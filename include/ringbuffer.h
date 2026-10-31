#ifndef RINGBUFFER_H
#define RINGBUFFER_H

#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>
#include "task.h"

#define RING_BUFFER_CAPACITY 1024
#define RING_BUFFER_MASK (RING_BUFFER_CAPACITY - 1)

typedef struct {
    taskdescriptor data;
    atomic_size_t sequence;
} ringbufferslot;

typedef struct {
    ringbufferslot slots[RING_BUFFER_CAPACITY];
    atomic_size_t enqueue_pos __attribute__((aligned(64)));
    atomic_size_t dequeue_pos __attribute__((aligned(64)));
} mpmcringbuffer;

void ringbuffer_init(mpmcringbuffer *rb);
bool ringbuffer_enqueue(mpmcringbuffer *rb, const taskdescriptor *task);
bool ringbuffer_dequeue(mpmcringbuffer *rb, taskdescriptor *out_task);

#endif