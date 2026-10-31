#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <signal.h>
#include "shm_context.h"
#include "loader.h"
#include "patcher.h"
#include "symbol_table.h"

typedef void (*gemm_fn)(const float*, const float*, float*, int, int, int);
typedef void (*layernorm_fn)(const float*, float*, int, float);
typedef void (*relu_fn)(const float*, float*, int);

static ShmContext *shm = NULL;
static volatile bool keep_running = true;

void handle_sigint(int sig) {
    (void)sig;
    keep_running = false;
}

int main(void) {
    printf("=====================================================\n");
    printf("=== KubePatch Daemon: Host GPU/CPU Dispatch Engine ===\n");
    printf("=====================================================\n");

    signal(SIGINT, handle_sigint);

    // 1. Create and map the POSIX Shared Memory segment
    shm_unlink(SHM_NAME); // Clean up any stale segment
    int fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
    if (fd < 0) {
        perror("[-] shm_open failed");
        return 1;
    }

    size_t total_size = sizeof(ShmContext);
    if (ftruncate(fd, total_size) != 0) {
        perror("[-] ftruncate failed");
        close(fd);
        return 1;
    }

    shm = (ShmContext *)mmap(NULL, total_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);

    if (shm == MAP_FAILED) {
        perror("[-] mmap failed");
        return 1;
    }

    // 2. Initialize the shared ring buffer and arena
    ringbuffer_init(&shm->ring);
    atomic_init(&shm->arena_offset, 0);
    atomic_init(&shm->tasks_completed, 0);
    atomic_init(&shm->daemon_ready, false);

    // 3. JIT-Load all 3 Kernels into Memory
    printf("[+] Loading in-memory kernels...\n");
    symboltable symtab;
    symtab_init(&symtab);

    // A. Load GEMM (Optimized SIMD)
    objectbinary obj_gemm;
    load_macho_object("kernels/gemm_opt.o", &obj_gemm);
    void *exec_gemm = create_exceutable_code(obj_gemm.file_bytes + obj_gemm.text_offset, obj_gemm.text_size);
    uint64_t gemm_off = 0; symbolbinding gemm_b;
    find_symbol_offset(&obj_gemm, "_gemm_compute", &gemm_off, &gemm_b);
    symtab_register(&symtab, "_gemm_compute", (uint8_t *)exec_gemm + gemm_off, gemm_b);

    // B. Load LayerNorm & ReLU from kernels/kernel.o
    objectbinary obj_k;
    load_macho_object("kernels/kernel.o", &obj_k);
    void *exec_k = create_exceutable_code(obj_k.file_bytes + obj_k.text_offset, obj_k.text_size);
    uint64_t ln_off = 0, relu_off = 0; symbolbinding b;
    find_symbol_offset(&obj_k, "_layernorm", &ln_off, &b);
    find_symbol_offset(&obj_k, "_vector_relu", &relu_off, &b);
    symtab_register(&symtab, "_layernorm", (uint8_t *)exec_k + ln_off, b);
    symtab_register(&symtab, "_vector_relu", (uint8_t *)exec_k + relu_off, b);

    gemm_fn run_gemm = (gemm_fn)symtab_lookup(&symtab, "_gemm_compute");
    layernorm_fn run_layernorm = (layernorm_fn)symtab_lookup(&symtab, "_layernorm");
    relu_fn run_relu = (relu_fn)symtab_lookup(&symtab, "_vector_relu");

    printf("[+] All 3 kernels loaded and registered!\n");
    atomic_store(&shm->daemon_ready, true);
    printf("[+] Daemon READY! Listening for K8s pod requests on %s...\n\n", SHM_NAME);

    // 4. Main Event Loop: Dequeue tasks and dispatch
    taskdescriptor task;
    while (keep_running) {
        if (ringbuffer_dequeue(&shm->ring, &task)) {
            // Dispatch based on kernel_id
            if (task.kernel_id == 1 && run_gemm) { // GEMM
                float *A = (float *)(shm->arena_data + task.input_buffer_offset);
                float *B = (float *)(shm->arena_data + task.input_buffer_offset + (task.dim_m * task.dim_k * sizeof(float)));
                float *C = (float *)(shm->arena_data + task.output_buffer_offset);
                run_gemm(A, B, C, task.dim_m, task.dim_n, task.dim_k);
            } 
            else if (task.kernel_id == 2 && run_layernorm) { // LayerNorm
                float *in = (float *)(shm->arena_data + task.input_buffer_offset);
                float *out = (float *)(shm->arena_data + task.output_buffer_offset);
                run_layernorm(in, out, task.dim_m, 1e-5f);
            } 
            else if (task.kernel_id == 3 && run_relu) { // ReLU
                float *in = (float *)(shm->arena_data + task.input_buffer_offset);
                float *out = (float *)(shm->arena_data + task.output_buffer_offset);
                run_relu(in, out, task.dim_m);
            }

            atomic_fetch_add(&shm->tasks_completed, 1);
        } else {
            #if defined(__aarch64__)
            __asm__ volatile("yield");
            #endif
        }
    }

    printf("\n[-] Daemon shutting down. Processed %zu tasks.\n", atomic_load(&shm->tasks_completed));
    shm_unlink(SHM_NAME);
    return 0;
}