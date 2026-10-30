#ifndef LOADER_H
#define LOADER_H

#include <stdint.h>
#include <stddef.h>

typedef struct{
    uint8_t *file_bytes;
    size_t file_size;

    uint32_t text_offset;
    uint64_t text_size;
    uint32_t symoff;
    uint32_t nsyms;
    uint32_t stroff;


}objectbinary;

int load_macho_object(const char *filepath, objectbinary *out_bin);

int find_symbol_offset(const objectbinary *bin, const char *symbol_name, uint64_t *out_offset);

void free_macho_object(objectbinary *bin);

#endif
