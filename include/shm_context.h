#ifndef SHM_CONTEXT_H
#define SHM_CONTEXT_H

#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>
#include "ringbuffer.h"

#define SHM_NAME "/kubepatch_shm"
#define ARENA_SHM_CAPACITY (64 * 1024 * 1024) // 64 MB for tensor data

// Structure mapped directly into /dev/shm
typedef struct {
    mpmcringbuffer ring;                // Lock-free task queue
    atomic_size_t arena_offset;        // Atomic bump pointer for tensor arena
    atomic_bool daemon_ready;          // Daemon signals clients when ready
    atomic_size_t tasks_completed;     // Global completion counter
    uint8_t arena_data[ARENA_SHM_CAPACITY]; // Raw shared tensor memory
} ShmContext;

#endif