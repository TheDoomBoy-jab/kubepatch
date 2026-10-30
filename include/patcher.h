#ifndef PATCHER_H
#define PATCHER_H

#include <stdint.h>
#include <stddef.h>

void *create_exceutable_code(const uint8_t *code_bytes, size_t code_size);

void free_executable_code(void *addr, size_t code_size);

#endif