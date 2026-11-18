#ifndef SHM_CONTEXT_H
#define SHM_CONTEXT_H

#include "ringbuffer.h"
#include "workload.h"

#define SHM_NAME "/kubepatch_v2"
#define SHM_ABI 2

typedef struct {
    atomic_uint ready;
    mpmcringbuffer ring;
    work_slot slots[WORK_SLOT_COUNT];
} ShmContext;

const char *shm_context_name(void);

#endif
