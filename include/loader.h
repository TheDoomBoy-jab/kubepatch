#ifndef LOADER_H
#define LOADER_H

#include <stdint.h>
#include <stddef.h>
#include "symbol_table.h"

typedef struct {
    const uint8_t *file_bytes;
    size_t file_size;
    uint32_t text_offset;
    uint64_t text_size;
    uint64_t text_address;
    uint32_t text_section;
    uint32_t symoff;
    uint32_t nsyms;
    uint32_t stroff;
    uint32_t strsize;
} objectbinary;

int load_macho_object(const char *filepath, objectbinary *out_bin);
int find_symbol_offset(const objectbinary *bin, const char *symbol_name, uint64_t *out_offset,
                       symbolbinding *out_binding);
void free_macho_object(objectbinary *bin);

#endif
