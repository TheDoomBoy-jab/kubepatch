#ifndef TASK_H
#define TASK_H

#include <stdint.h>

typedef struct {
    uint64_t task_id;
    uint32_t client_id;
    uint32_t kernel_id;

    uint64_t input_buffer_offset;
    uint64_t output_buffer_offset;

    uint32_t memory_bytes;
    uint32_t compute_flops;
    uint64_t deadline_ns;

    uint16_t dim_m;
    uint16_t dim_n;
    uint16_t dim_k;
    uint8_t  priority;
    uint8_t  status;
    uint8_t  flags;
    uint8_t  _reserved[5];
} __attribute__((aligned(64))) taskdescriptor;

#endif