#ifndef ARENA_H
#define ARENA_H

#include <stdint.h>
#include <stddef.h>
#include <stdatomic.h>

typedef struct {
    uint8_t *buffer;              // Contiguous pre-allocated RAM
    size_t capacity;              // Total bytes (e.g. 64 MB)
    atomic_size_t current_offset; // Atomic bump pointer
} MemoryArena;

int arena_init(MemoryArena *arena, size_t capacity);
int64_t arena_alloc_offset(MemoryArena *arena, size_t size, size_t alignment);
void *arena_get_ptr(MemoryArena *arena, uint64_t offset);
void arena_reset(MemoryArena *arena);
void arena_destroy(MemoryArena *arena);

#endif