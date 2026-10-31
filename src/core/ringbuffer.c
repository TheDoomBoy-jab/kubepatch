#include "ringbuffer.h"
#include <string.h>

void ringbuffer_init(mpmcringbuffer *rb){
    if (!rb) return;

    for(size_t i=0; i<RING_BUFFER_CAPACITY; i++){
        atomic_init(&rb->slots[i].sequence, i);


    }

    atomic_init(&rb->enqueue_pos, 0);
    atomic_init(&rb->dequeue_pos, 0);
}

bool ringbuffer_enqueue(mpmcringbuffer *rb, const taskdescriptor *task) {
    size_t pos = atomic_load_explicit(&rb->enqueue_pos, memory_order_relaxed);
    for (;;) {
        ringbufferslot *slot = &rb->slots[pos & RING_BUFFER_MASK];
        size_t seq = atomic_load_explicit(&slot->sequence, memory_order_acquire);
        intptr_t diff = (intptr_t)seq - (intptr_t)pos;
        if (diff == 0) {

            if (atomic_compare_exchange_weak_explicit(&rb->enqueue_pos, &pos, pos + 1,
                                                      memory_order_relaxed, memory_order_relaxed)) {

                slot->data = *task;

                atomic_store_explicit(&slot->sequence, pos + 1, memory_order_release);
                return true;
            }
        } else if (diff < 0) {

            return false;
        } else {

            pos = atomic_load_explicit(&rb->enqueue_pos, memory_order_relaxed);
        }
    }
}


bool ringbuffer_dequeue(mpmcringbuffer *rb, taskdescriptor *out_task) {
    size_t pos = atomic_load_explicit(&rb->dequeue_pos, memory_order_relaxed);
    for (;;) {
        ringbufferslot *slot = &rb->slots[pos & RING_BUFFER_MASK];
        size_t seq = atomic_load_explicit(&slot->sequence, memory_order_acquire);
        intptr_t diff = (intptr_t)seq - (intptr_t)(pos + 1);
        if (diff == 0) {

            if (atomic_compare_exchange_weak_explicit(&rb->dequeue_pos, &pos, pos + 1,
                                                      memory_order_relaxed, memory_order_relaxed)) {

                *out_task = slot->data;

                atomic_store_explicit(&slot->sequence, pos + RING_BUFFER_MASK + 1, memory_order_release);
                return true;
            }
        } else if (diff < 0) {

            return false;
        } else {

            pos = atomic_load_explicit(&rb->dequeue_pos, memory_order_relaxed);
        }
    }
}
