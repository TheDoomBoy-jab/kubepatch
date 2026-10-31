#include "arena.h"
#include <stdio.h>
#include <sys/mman.h>
#include <unistd.h>

int arena_init(MemoryArena *arena, size_t capacity) {
    if (!arena || capacity == 0) return -1;

    arena->buffer = (uint8_t *)mmap(NULL, capacity, PROT_READ | PROT_WRITE,
                                    MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    if (arena->buffer == MAP_FAILED) {
        perror("[-] arena mmap failed");
        return -1;
    }

    arena->capacity = capacity;
    atomic_init(&arena->current_offset, 0);
    return 0;
}

int64_t arena_alloc_offset(MemoryArena *arena, size_t size, size_t alignment) {
    if (!arena || size == 0) return -1;

    size_t prev_offset;
    size_t aligned_offset;
    size_t new_offset;

    // Lock-free atomic Compare-And-Swap (CAS) to safely bump the pointer
    do {
        prev_offset = atomic_load(&arena->current_offset);

        // Align the offset (e.g. to 64 bytes for cacheline/SIMD alignment)
        aligned_offset = (prev_offset + (alignment - 1)) & ~(alignment - 1);
        new_offset = aligned_offset + size;

        if (new_offset > arena->capacity) {
            return -1; // Arena full
        }

    } while (!atomic_compare_exchange_weak(&arena->current_offset, &prev_offset, new_offset));

    return (int64_t)aligned_offset;
}

void *arena_get_ptr(MemoryArena *arena, uint64_t offset) {
    if (!arena || offset >= arena->capacity) return NULL;
    return (void *)(arena->buffer + offset);
}

void arena_reset(MemoryArena *arena) {
    if (!arena) return;
    atomic_store(&arena->current_offset, 0);
}

void arena_destroy(MemoryArena *arena) {
    if (arena && arena->buffer && arena->buffer != MAP_FAILED) {
        munmap(arena->buffer, arena->capacity);
        arena->buffer = NULL;
        arena->capacity = 0;
    }
}