#include "loader.h"

#include <fcntl.h>
#include <mach-o/loader.h>
#include <mach-o/nlist.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

static int has_range(size_t length, uint64_t offset, uint64_t bytes) {
    return offset <= length && bytes <= length - offset;
}

static int read_sections(const uint8_t *command, uint32_t cmdsize, objectbinary *bin,
                         uint32_t *section_index) {
    struct segment_command_64 segment;
    if (cmdsize < sizeof(segment)) {
        return -1;
    }
    memcpy(&segment, command, sizeof(segment));
    if (segment.nsects > (cmdsize - sizeof(segment)) / sizeof(struct section_64) ||
        segment.nsects > UINT32_MAX - *section_index) {
        return -1;
    }
    for (uint32_t i = 0; i < segment.nsects; i++) {
        struct section_64 section;
        memcpy(&section, command + sizeof(segment) + i * sizeof(section), sizeof(section));
        ++*section_index;
        if (strncmp(section.segname, "__TEXT", sizeof(section.segname)) != 0 ||
            strncmp(section.sectname, "__text", sizeof(section.sectname)) != 0) {
            continue;
        }
        if (bin->text_section || section.nreloc || !section.size ||
            !has_range(bin->file_size, section.offset, section.size)) {
            return -1;
        }
        bin->text_section = *section_index;
        bin->text_address = section.addr;
        bin->text_offset = section.offset;
        bin->text_size = section.size;
    }
    return 0;
}

static int read_commands(objectbinary *bin) {
    struct mach_header_64 header;
    memcpy(&header, bin->file_bytes, sizeof(header));
    if (header.magic != MH_MAGIC_64 || header.filetype != MH_OBJECT ||
        header.cputype != CPU_TYPE_ARM64 ||
        !has_range(bin->file_size, sizeof(header), header.sizeofcmds)) {
        return -1;
    }

    size_t offset = sizeof(header);
    size_t end = offset + header.sizeofcmds;
    uint32_t section_index = 0;
    int found_symbols = 0;
    for (uint32_t i = 0; i < header.ncmds; i++) {
        struct load_command command;
        if (!has_range(end, offset, sizeof(command))) {
            return -1;
        }
        memcpy(&command, bin->file_bytes + offset, sizeof(command));
        if (command.cmdsize < sizeof(command) || command.cmdsize % 8 != 0 ||
            !has_range(end, offset, command.cmdsize)) {
            return -1;
        }

        const uint8_t *bytes = bin->file_bytes + offset;
        if (command.cmd == LC_SEGMENT_64) {
            if (read_sections(bytes, command.cmdsize, bin, &section_index) != 0) {
                return -1;
            }
        } else if (command.cmd == LC_SYMTAB) {
            struct symtab_command table;
            if (found_symbols || command.cmdsize < sizeof(table)) {
                return -1;
            }
            memcpy(&table, bytes, sizeof(table));
            if (!has_range(bin->file_size, table.symoff, (uint64_t)table.nsyms * sizeof(struct nlist_64)) ||
                !has_range(bin->file_size, table.stroff, table.strsize)) {
                return -1;
            }
            bin->symoff = table.symoff;
            bin->nsyms = table.nsyms;
            bin->stroff = table.stroff;
            bin->strsize = table.strsize;
            found_symbols = 1;
        }
        offset += command.cmdsize;
    }

    return offset == end && found_symbols && bin->text_section && bin->text_section <= UINT8_MAX ? 0 : -1;
}

int load_macho_object(const char *filepath, objectbinary *bin) {
    if (!filepath || !bin) {
        return -1;
    }
    *bin = (objectbinary){0};
    int fd = open(filepath, O_RDONLY);
    if (fd < 0) {
        perror(filepath);
        return -1;
    }
    struct stat st;
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size < (off_t)sizeof(struct mach_header_64) ||
        (uintmax_t)st.st_size > SIZE_MAX) {
        fprintf(stderr, "%s: invalid object file size or type\n", filepath);
        close(fd);
        return -1;
    }
    size_t size = (size_t)st.st_size;
    void *bytes = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (bytes == MAP_FAILED) {
        perror(filepath);
        return -1;
    }
    bin->file_bytes = bytes;
    bin->file_size = size;
    if (read_commands(bin) != 0) {
        fprintf(stderr, "%s: expected a valid ARM64 Mach-O object without text relocations\n", filepath);
        free_macho_object(bin);
        return -1;
    }
    return 0;
}

int find_symbol_offset(const objectbinary *bin, const char *name, uint64_t *out_offset,
                       symbolbinding *out_binding) {
    if (!bin || !bin->file_bytes || !name || !out_offset) {
        return -1;
    }
    const char *strings = (const char *)bin->file_bytes + bin->stroff;
    for (uint32_t i = 0; i < bin->nsyms; i++) {
        struct nlist_64 symbol;
        memcpy(&symbol, bin->file_bytes + bin->symoff + (size_t)i * sizeof(symbol), sizeof(symbol));
        const uint32_t index = symbol.n_un.n_strx;
        if ((symbol.n_type & N_STAB) || (symbol.n_type & N_TYPE) != N_SECT ||
            symbol.n_sect != bin->text_section || index >= bin->strsize ||
            !memchr(strings + index, '\0', bin->strsize - index) ||
            strcmp(strings + index, name) != 0) {
            continue;
        }
        if (symbol.n_value < bin->text_address ||
            symbol.n_value - bin->text_address >= bin->text_size ||
            (symbol.n_value - bin->text_address) % 4 != 0) {
            return -1;
        }
        *out_offset = symbol.n_value - bin->text_address;
        if (out_binding) {
            *out_binding = symbol.n_desc & N_WEAK_DEF ? SYM_WEAK : SYM_STRONG;
        }
        return 0;
    }
    return -1;
}

void free_macho_object(objectbinary *bin) {
    if (bin && bin->file_bytes) {
        munmap((void *)bin->file_bytes, bin->file_size);
        *bin = (objectbinary){0};
    }
}
