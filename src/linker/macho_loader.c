#include "loader.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <mach-o/loader.h>
#include <mach-o/nlist.h>

#include "symbol_table.h"
int load_macho_object(const char *filepath, objectbinary *out_bin) {
    if (!filepath || !out_bin) return -1;
    memset(out_bin, 0, sizeof(objectbinary));
    int fd = open(filepath, O_RDONLY);
    if (fd < 0) {
        perror("[-] open failed");
        return -1;
    }
    struct stat st;
    if (fstat(fd, &st) != 0) {
        perror("[-] fstat failed");
        close(fd);
        return -1;
    }
    out_bin->file_size = st.st_size;
    out_bin->file_bytes = mmap(NULL, out_bin->file_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (out_bin->file_bytes == MAP_FAILED) {
        perror("[-] mmap failed");
        return -1;
    }
    struct mach_header_64 *header = (struct mach_header_64 *)out_bin->file_bytes;
    if (header->magic != MH_MAGIC_64) {
        fprintf(stderr, "[-] Error: %s is not a 64-bit Mach-O binary (magic: 0x%x)\n", filepath, header->magic);
        munmap(out_bin->file_bytes, out_bin->file_size);
        return -1;
    }
    uint8_t *cmd_ptr = out_bin->file_bytes + sizeof(struct mach_header_64);
    int found_text = 0;
    int found_symtab = 0;
    for (uint32_t i = 0; i < header->ncmds; i++) {
        struct load_command *lc = (struct load_command *)cmd_ptr;
        if (lc->cmd == LC_SEGMENT_64) {
            struct segment_command_64 *seg = (struct segment_command_64 *)lc;
            struct section_64 *sec = (struct section_64 *)(cmd_ptr + sizeof(struct segment_command_64));
            for (uint32_t s = 0; s < seg->nsects; s++) {
                if (strncmp(sec[s].sectname, "__text", 6) == 0) {
                    out_bin->text_offset = sec[s].offset;
                    out_bin->text_size = sec[s].size;
                    found_text = 1;
                }
            }
        } else if (lc->cmd == LC_SYMTAB) {
            struct symtab_command *symtab = (struct symtab_command *)lc;
            out_bin->symoff = symtab->symoff;
            out_bin->nsyms = symtab->nsyms;
            out_bin->stroff = symtab->stroff;
            found_symtab = 1;
        }
        cmd_ptr += lc->cmdsize;
    }

    if(!found_text || !found_symtab){
        fprintf(stderr, "[-] Error: Failed to find __text section or symbol table in %s\n", filepath);
        munmap(out_bin->file_bytes, out_bin->file_size);
        return -1;
    }

    return 0;

}

int find_symbol_offset(const objectbinary *bin, const char *symbol_name, uint64_t *out_offset, symbolbinding *out_binding ) {
    if (!bin || !symbol_name || !out_offset) return -1;
     struct nlist_64 *syms = (struct nlist_64 *)(bin->file_bytes + bin->symoff);
    const char *strtab = (const char *)(bin->file_bytes + bin->stroff);
    for (uint32_t i = 0; i < bin->nsyms; i++) {
        const char *name = strtab + syms[i].n_un.n_strx;
        if (strcmp(name, symbol_name) == 0) {
            *out_offset = syms[i].n_value;
            if (out_binding) {
                // In Mach-O, bit 0x0080 (N_WEAK_DEF) in n_desc marks a weak symbol definition
                if (syms[i].n_desc & N_WEAK_DEF) {
                    *out_binding = SYM_WEAK;
                } else {
                    *out_binding = SYM_STRONG;
                }
            }
            return 0;
        }
    }
    return -1; // Symbol not found
}
void free_macho_object(objectbinary *bin) {
    if (bin && bin->file_bytes && bin->file_bytes != MAP_FAILED) {
        munmap(bin->file_bytes, bin->file_size);
        bin->file_bytes = NULL;
    }
}