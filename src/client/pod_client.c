#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <time.h>
#include "shm_context.h"

int main(int argc, char **argv) {
    uint32_t pod_id = 1;
    int num_tasks = 10000;
    if (argc > 1) pod_id = (uint32_t)atoi(argv[1]);
    if (argc > 2) num_tasks = atoi(argv[2]);

    // 1. Connect to shared memory
    int fd = shm_open(SHM_NAME, O_RDWR, 0666);
    if (fd < 0) {
        fprintf(stderr, "[-] Pod %u: Daemon is not running! Cannot open %s\n", pod_id, SHM_NAME);
        return 1;
    }

    ShmContext *shm = (ShmContext *)mmap(NULL, sizeof(ShmContext), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);

    if (shm == MAP_FAILED) {
        perror("[-] mmap failed");
        return 1;
    }

    // Wait for daemon to be ready
    while (!atomic_load(&shm->daemon_ready)) {
        usleep(1000);
    }

    printf("[Pod %u] Connected to KubePatch daemon. Submitting %d mixed tensor tasks...\n", pod_id, num_tasks);

    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    for (int i = 0; i < num_tasks; i++) {
        // Allocate space in shared arena
        size_t bytes = 16 * 16 * sizeof(float) * 3;
        size_t prev_off = atomic_fetch_add(&shm->arena_offset, bytes);
        if (prev_off + bytes >= ARENA_SHM_CAPACITY) {
            atomic_store(&shm->arena_offset, 0);
            prev_off = 0;
        }

        taskdescriptor task;
        task.task_id = ((uint64_t)pod_id << 32) | i;
        task.client_id = pod_id;
        
        // Rotate through all 3 kernels!
        // 1 = GEMM, 2 = LayerNorm, 3 = ReLU
        task.kernel_id = (i % 3) + 1;
        task.input_buffer_offset = prev_off;
        task.output_buffer_offset = prev_off + (16 * 16 * sizeof(float) * 2);
        task.dim_m = 16;
        task.dim_n = 16;
        task.dim_k = 16;

        // Push to lock-free ring buffer
        while (!ringbuffer_enqueue(&shm->ring, &task)) {
            #if defined(__aarch64__)
            __asm__ volatile("yield");
            #endif
        }
    }

    clock_gettime(CLOCK_MONOTONIC, &end);
    double elapsed = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;
    printf("[Pod %u] Completed submitting %d tasks in %.4f sec (%.2f tasks/sec)\n", 
           pod_id, num_tasks, elapsed, (double)num_tasks / elapsed);

    return 0;
}