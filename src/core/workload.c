#include "workload.h"

#include <math.h>
#include <time.h>

uint64_t monotonic_ns(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000000000ULL + (uint64_t)now.tv_nsec;
}

void poll_pause(void) {
    #if defined(__aarch64__)
    __asm__ volatile("yield");
    #endif
}

void work_slots_init(work_slot slots[WORK_SLOT_COUNT]) {
    for (size_t i = 0; i < WORK_SLOT_COUNT; i++) {
        atomic_init(&slots[i].state, WORK_FREE);
    }
}

static float gemm_a(size_t row, size_t col, unsigned seed) {
    return (float)(row + 1 + col % 4 + seed);
}

static float gemm_b(size_t row, size_t col) {
    return (float)((int)(row % 3) + (int)(col % 5) - 3);
}

taskdescriptor work_prepare(work_slot slots[WORK_SLOT_COUNT], size_t index, uint64_t task_id,
                            uint32_t client_id, enum kernel_id kernel) {
    work_slot *slot = &slots[index];
    slot->task_id = task_id;
    size_t length = kernel == KERNEL_GEMM ? MATRIX_ELEMENTS : MATRIX_DIM;
    for (size_t i = 0; i < length; i++) {
        slot->output[i] = NAN;
        if (kernel == KERNEL_GEMM) {
            slot->input[i] = gemm_a(i / MATRIX_DIM, i % MATRIX_DIM, task_id % 7);
            slot->input[MATRIX_ELEMENTS + i] = gemm_b(i / MATRIX_DIM, i % MATRIX_DIM);
        } else {
            slot->input[i] = (float)((int)i - 7) + (float)(task_id % 7);
        }
    }
    taskdescriptor task = {
        .task_id = task_id,
        .client_id = client_id,
        .kernel_id = kernel,
        .input_buffer_offset = index * sizeof(*slot) + offsetof(work_slot, input),
        .output_buffer_offset = index * sizeof(*slot) + offsetof(work_slot, output),
        .dim_m = MATRIX_DIM,
        .dim_n = MATRIX_DIM,
        .dim_k = MATRIX_DIM,
    };
    slot->started_ns = monotonic_ns();
    atomic_store_explicit(&slot->state, WORK_QUEUED, memory_order_release);
    return task;
}

bool work_execute(work_slot slots[WORK_SLOT_COUNT], const taskdescriptor *task, kernel_dispatch dispatch) {
    size_t index = task->input_buffer_offset / sizeof(work_slot);
    if (index >= WORK_SLOT_COUNT ||
        task->input_buffer_offset != index * sizeof(work_slot) + offsetof(work_slot, input) ||
        task->output_buffer_offset != index * sizeof(work_slot) + offsetof(work_slot, output)) {
        return false;
    }
    work_slot *slot = &slots[index];
    unsigned expected = WORK_QUEUED;
    if (!atomic_compare_exchange_strong_explicit(&slot->state, &expected, WORK_RUNNING,
                                                  memory_order_acquire, memory_order_relaxed)) {
        return false;
    }
    bool valid = slot->task_id == task->task_id && task->dim_m == MATRIX_DIM &&
                 task->dim_n == MATRIX_DIM && task->dim_k == MATRIX_DIM;
    if (valid && task->kernel_id == KERNEL_GEMM && dispatch.gemm) {
        dispatch.gemm(slot->input, slot->input + MATRIX_ELEMENTS, slot->output,
                      MATRIX_DIM, MATRIX_DIM, MATRIX_DIM);
    } else if (valid && task->kernel_id == KERNEL_LAYERNORM && dispatch.layernorm) {
        dispatch.layernorm(slot->input, slot->output, MATRIX_DIM, 1e-5f);
    } else if (valid && task->kernel_id == KERNEL_RELU && dispatch.relu) {
        dispatch.relu(slot->input, slot->output, MATRIX_DIM);
    } else {
        valid = false;
    }
    slot->finished_ns = monotonic_ns();
    atomic_store_explicit(&slot->state, valid ? WORK_DONE : WORK_FAILED, memory_order_release);
    return valid;
}

bool work_verify(const work_slot *slot, enum kernel_id kernel) {
    if (atomic_load_explicit(&slot->state, memory_order_acquire) != WORK_DONE) {
        return false;
    }
    if (kernel == KERNEL_GEMM) {
        // For k=0..15, sum(k%3 - 1)=-1 and sum((k%4)*(k%3 - 1))=-1.
        // Expanding A[row,k]*B[k,col] gives an independent, exact reference.
        for (size_t i = 0; i < MATRIX_ELEMENTS; i++) {
            int row_base = (int)(i / MATRIX_DIM + 1 + slot->task_id % 7);
            int col_base = (int)(i % MATRIX_DIM % 5) - 2;
            float expected = (float)((16 * row_base + 24) * col_base - row_base - 1);
            if (slot->output[i] != expected) {
                return false;
            }
        }
        return true;
    }
    for (size_t i = 0; i < MATRIX_DIM; i++) {
        float expected = kernel == KERNEL_RELU ?
            fmaxf(0.0f, slot->input[i]) : ((float)i - 7.5f) / sqrtf(21.25f + 1e-5f);
        if (!(fabsf(slot->output[i] - expected) <= 1e-5f)) {
            return false;
        }
    }
    return true;
}
