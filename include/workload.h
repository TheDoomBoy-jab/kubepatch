#ifndef WORKLOAD_H
#define WORKLOAD_H

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include "kernel.h"
#include "task.h"

#define MATRIX_DIM 16
#define MATRIX_ELEMENTS (MATRIX_DIM * MATRIX_DIM)
#define WORK_SLOT_COUNT 256
#define GEMM_SYMBOL "_gemm_compute"

enum kernel_id { KERNEL_GEMM = 1, KERNEL_LAYERNORM, KERNEL_RELU };
enum work_state { WORK_FREE, WORK_RESERVED, WORK_QUEUED, WORK_RUNNING, WORK_DONE, WORK_FAILED };

typedef struct {
    atomic_uint state;
    uint64_t task_id;
    uint64_t started_ns;
    uint64_t finished_ns;
    _Alignas(64) float input[2 * MATRIX_ELEMENTS];
    _Alignas(64) float output[MATRIX_ELEMENTS];
} work_slot;

typedef struct {
    gemm_fn gemm;
    layernorm_fn layernorm;
    relu_fn relu;
} kernel_dispatch;

uint64_t monotonic_ns(void);
void poll_pause(void);
void work_slots_init(work_slot slots[WORK_SLOT_COUNT]);
taskdescriptor work_prepare(work_slot slots[WORK_SLOT_COUNT], size_t index, uint64_t task_id,
                            uint32_t client_id, enum kernel_id kernel);
bool work_execute(work_slot slots[WORK_SLOT_COUNT], const taskdescriptor *task, kernel_dispatch dispatch);
bool work_verify(const work_slot *slot, enum kernel_id kernel);

#endif
