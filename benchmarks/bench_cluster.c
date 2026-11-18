#include "ringbuffer.h"
#include "workload.h"

#include <inttypes.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PRODUCERS 8
#define WORKERS 2
#define JOBS_PER_PRODUCER 25000
#define TOTAL_JOBS (PRODUCERS * JOBS_PER_PRODUCER)
#define WINDOW (WORK_SLOT_COUNT / PRODUCERS)
#define SWAP_AFTER 1000

_Static_assert(WORK_SLOT_COUNT % PRODUCERS == 0, "producer windows must divide arena");

static mpmcringbuffer queue;
static work_slot slots[WORK_SLOT_COUNT];
static symboltable symbols;
static kernel_image fallback, simd;
static uint64_t latencies[TOTAL_JOBS];
static atomic_bool stopping;
static atomic_bool upgraded;
static atomic_size_t fallback_calls;
static atomic_size_t simd_calls;

typedef struct {
    unsigned index;
    bool mixed;
    size_t verified;
} producer_state;

static void *produce(void *arg) {
    producer_state *producer = arg;
    unsigned next = 0;
    unsigned checked = 0;

    while (checked < JOBS_PER_PRODUCER && !atomic_load(&stopping)) {
        bool can_submit = next < JOBS_PER_PRODUCER && next - checked < WINDOW &&
                          (!producer->mixed || next < JOBS_PER_PRODUCER / 2 ||
                           atomic_load(&upgraded));
        if (can_submit) {
            size_t index = producer->index * WINDOW + next % WINDOW;
            uint64_t id = ((uint64_t)producer->index << 32) | next;
            taskdescriptor task = work_prepare(slots, index, id, producer->index, KERNEL_GEMM);
            while (!atomic_load(&stopping) && !ringbuffer_enqueue(&queue, &task)) {
                poll_pause();
            }
            ++next;
        }

        if (checked < next) {
            work_slot *slot = &slots[producer->index * WINDOW + checked % WINDOW];
            unsigned state = atomic_load_explicit(&slot->state, memory_order_acquire);
            if (state == WORK_DONE || state == WORK_FAILED) {
                if (work_verify(slot, KERNEL_GEMM)) {
                    ++producer->verified;
                }
                latencies[producer->index * JOBS_PER_PRODUCER + checked] = slot->finished_ns - slot->started_ns;
                ++checked;
                continue;
            }
        }
        if (!can_submit) {
            poll_pause();
        }
    }
    return NULL;
}

static void *consume(void *unused) {
    (void)unused;
    while (!atomic_load(&stopping)) {
        taskdescriptor task;
        if (!ringbuffer_dequeue(&queue, &task)) {
            poll_pause();
            continue;
        }

        void *entry = symtab_lookup(&symbols, GEMM_SYMBOL);
        kernel_dispatch dispatch = {.gemm = (gemm_fn)entry};
        if (work_execute(slots, &task, dispatch)) {
            // The SIMD image is prepared and published by main. Workers need
            // only compare against the immutable fallback entry.
            atomic_size_t *counter = entry == fallback.entry ? &fallback_calls : &simd_calls;
            atomic_fetch_add_explicit(counter, 1, memory_order_relaxed);
        } else {
            atomic_store(&stopping, true);
        }
    }
    return NULL;
}

static int compare_ns(const void *left, const void *right) {
    uint64_t a = *(const uint64_t *)left;
    uint64_t b = *(const uint64_t *)right;
    return (a > b) - (a < b);
}

static int publish(kernel_image *image) {
    return symtab_register(&symbols, GEMM_SYMBOL, image->entry, image->binding);
}

int main(int argc, char **argv) {
    const char *mode = argc == 2 ? argv[1] : "mixed";
    bool mixed = strcmp(mode, "mixed") == 0;
    bool simd_only = strcmp(mode, "simd") == 0;
    if (argc > 2 || (!mixed && !simd_only && strcmp(mode, "fallback") != 0)) {
        fprintf(stderr, "Usage: %s [mixed|fallback|simd]\n", argv[0]);
        return 2;
    }

    ringbuffer_init(&queue);
    work_slots_init(slots);
    if (symtab_init(&symbols) != 0) {
        fprintf(stderr, "Could not initialize symbol registry\n");
        return 1;
    }
    int status = 1;
    if (simd_only) {
        if (kernel_load("kernels/gemm_opt.o", GEMM_SYMBOL, &simd) ||
            simd.binding != SYM_STRONG || publish(&simd)) {
            goto cleanup;
        }
    } else if (kernel_load("kernels/gemm_fallback.o", GEMM_SYMBOL, &fallback) ||
               fallback.binding != SYM_WEAK || publish(&fallback)) {
        goto cleanup;
    }

    pthread_t workers[WORKERS], producers[PRODUCERS];
    producer_state states[PRODUCERS] = {0};
    unsigned worker_count = 0, producer_count = 0;
    for (; worker_count < WORKERS; ++worker_count) {
        if (pthread_create(&workers[worker_count], NULL, consume, NULL) != 0) {
            break;
        }
    }
    printf("KubePatch %s benchmark | %d producer threads | %d worker threads | %d GEMM tasks\n",
           mode, PRODUCERS, WORKERS, TOTAL_JOBS);
    uint64_t start = monotonic_ns();
    if (worker_count == WORKERS) {
        for (; producer_count < PRODUCERS; ++producer_count) {
            states[producer_count] = (producer_state){.index = producer_count, .mixed = mixed};
            if (pthread_create(&producers[producer_count], NULL, produce, &states[producer_count]) != 0) {
                break;
            }
        }
    }
    bool failed = producer_count != PRODUCERS;
    if (failed) {
        fprintf(stderr, "Could not launch all benchmark threads\n");
    } else if (mixed) {
        while (atomic_load_explicit(&fallback_calls, memory_order_relaxed) < SWAP_AFTER &&
               !atomic_load(&stopping)) {
            poll_pause();
        }
        failed = atomic_load(&stopping) ||
                 kernel_load("kernels/gemm_opt.o", GEMM_SYMBOL, &simd) ||
                 simd.binding != SYM_STRONG || publish(&simd);
        if (!failed) {
            puts("Published strong SIMD _gemm_compute over the weak fallback");
            atomic_store(&upgraded, true);
        }
    }
    if (failed) {
        atomic_store(&stopping, true);
    }

    size_t verified = 0;
    for (unsigned i = 0; i < producer_count; ++i) {
        pthread_join(producers[i], NULL);
        verified += states[i].verified;
    }
    uint64_t end = monotonic_ns();
    atomic_store(&stopping, true);
    for (unsigned i = 0; i < worker_count; ++i) {
        pthread_join(workers[i], NULL);
    }
    size_t scalar = atomic_load(&fallback_calls);
    size_t vector = atomic_load(&simd_calls);
    if (failed || verified != TOTAL_JOBS || scalar + vector != TOTAL_JOBS ||
        (mixed && (!scalar || !vector))) {
        fprintf(stderr, "Benchmark failed: %zu verified, %zu fallback, %zu SIMD\n", verified, scalar, vector);
        goto cleanup;
    }

    qsort(latencies, TOTAL_JOBS, sizeof(uint64_t), compare_ns);
    double elapsed = (double)(end - start) / 1e9;
    printf("Verified tasks: %zu (%zu fallback, %zu SIMD)\n", verified, scalar, vector);
    printf("Elapsed: %.4f s\n", elapsed);
    printf("Throughput: %.0f verified tasks/s\n", verified / elapsed);
    printf("Latency (submit through dispatch): P50 %.3f ms, P99 %.3f ms\n",
           latencies[(TOTAL_JOBS * 50 + 99) / 100 - 1] / 1e6,
           latencies[(TOTAL_JOBS * 99 + 99) / 100 - 1] / 1e6);
    status = 0;

cleanup:
    kernel_unload(&simd);
    kernel_unload(&fallback);
    symtab_destroy(&symbols);
    return status;
}
