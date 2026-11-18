#include "kernel.h"
#include "patcher.h"

#include <stdio.h>

int kernel_load(const char *path, const char *symbol, kernel_image *kernel) {
    *kernel = (kernel_image){0};
    objectbinary object;
    if (load_macho_object(path, &object) != 0) {
        return -1;
    }
    uint64_t offset;
    if (find_symbol_offset(&object, symbol, &offset, &kernel->binding) != 0) {
        fprintf(stderr, "%s: missing or invalid text symbol %s\n", path, symbol);
        free_macho_object(&object);
        return -1;
    }
    kernel->size = (size_t)object.text_size;
    kernel->code = create_executable_code(object.file_bytes + object.text_offset, kernel->size);
    free_macho_object(&object);
    if (!kernel->code) {
        return -1;
    }
    kernel->entry = (uint8_t *)kernel->code + offset;
    return 0;
}

void kernel_unload(kernel_image *kernel) {
    free_executable_code(kernel->code, kernel->size);
    *kernel = (kernel_image){0};
}
