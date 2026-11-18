#include "shm_context.h"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#define TIMEOUT_NS (10ULL * 1000000000)

static bool parse_count(const char *value, uint32_t *out) {
    char *end;
    errno = 0;
    unsigned long parsed = strtoul(value, &end, 10);
    if (errno || *value < '0' || *value > '9' || *end || parsed == 0 || parsed > UINT32_MAX) {
        return false;
    }
    *out = (uint32_t)parsed;
    return true;
}

static size_t reserve_slot(ShmContext *shm) {
    uint64_t start = monotonic_ns();
    while (atomic_load_explicit(&shm->ready, memory_order_acquire) == SHM_ABI &&
           monotonic_ns() - start < TIMEOUT_NS) {
        for (size_t i = 0; i < WORK_SLOT_COUNT; i++) {
            unsigned expected = WORK_FREE;
            if (atomic_compare_exchange_strong_explicit(&shm->slots[i].state, &expected,
                                                         WORK_RESERVED, memory_order_acquire,
                                                         memory_order_relaxed)) {
                return i;
            }
        }
        poll_pause();
    }
    return WORK_SLOT_COUNT;
}

static bool run_task(ShmContext *shm, size_t index, uint32_t client, uint32_t sequence) {
    enum kernel_id kernel = KERNEL_GEMM + sequence % 3;
    uint64_t id = ((uint64_t)client << 32) | sequence;
    work_slot *slot = &shm->slots[index];
    taskdescriptor task = work_prepare(shm->slots, index, id, client, kernel);
    while (!ringbuffer_enqueue(&shm->ring, &task)) {
        if (atomic_load_explicit(&shm->ready, memory_order_acquire) != SHM_ABI ||
            monotonic_ns() - slot->started_ns >= TIMEOUT_NS) {
            atomic_store_explicit(&slot->state, WORK_FREE, memory_order_release);
            return false;
        }
        poll_pause();
    }

    for (;;) {
        unsigned state = atomic_load_explicit(&slot->state, memory_order_acquire);
        if (state == WORK_DONE || state == WORK_FAILED) {
            bool ok = work_verify(slot, kernel);
            atomic_store_explicit(&slot->state, WORK_FREE, memory_order_release);
            return ok;
        }
        if (atomic_load_explicit(&shm->ready, memory_order_acquire) != SHM_ABI ||
            monotonic_ns() - slot->started_ns >= TIMEOUT_NS) {
            // Leave owned storage alone because the daemon may still be using it.
            return false;
        }
        poll_pause();
    }
}

int main(int argc, char **argv) {
    uint32_t client = 1;
    uint32_t count = 10000;
    if (argc > 3 || (argc > 1 && !parse_count(argv[1], &client)) ||
        (argc > 2 && !parse_count(argv[2], &count))) {
        fprintf(stderr, "Usage: %s [positive-client-id] [positive-task-count]\n", argv[0]);
        return 2;
    }

    const char *name = shm_context_name();
    int fd = shm_open(name, O_RDWR, 0);
    if (fd < 0) {
        perror(name);
        return 1;
    }
    struct stat st;
    if (fstat(fd, &st) != 0 || st.st_size < (off_t)sizeof(ShmContext)) {
        fprintf(stderr, "%s: daemon is starting or uses another shared-memory layout\n", name);
        close(fd);
        return 1;
    }
    ShmContext *shm = mmap(NULL, sizeof(*shm), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (shm == MAP_FAILED) {
        perror("mmap");
        return 1;
    }
    if (atomic_load_explicit(&shm->ready, memory_order_acquire) != SHM_ABI) {
        fprintf(stderr, "%s: daemon is not ready\n", name);
        munmap(shm, sizeof(*shm));
        return 1;
    }
    for (uint32_t i = 0; i < count; i++) {
        size_t index = reserve_slot(shm);
        if (index == WORK_SLOT_COUNT || !run_task(shm, index, client, i)) {
            fprintf(stderr, "Client %" PRIu32 ": task %" PRIu32 " timed out or failed validation\n", client, i);
            munmap(shm, sizeof(*shm));
            return 1;
        }
    }
    munmap(shm, sizeof(*shm));
    printf("Client %" PRIu32 " verified %" PRIu32 " tasks\n", client, count);
    return 0;
}
