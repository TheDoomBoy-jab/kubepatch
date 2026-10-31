#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "loader.h"
#include "patcher.h"
#include "symbol_table.h"

typedef void (*gemm_fn)(const float*, const float*, float*, int, int, int);

// Helper to benchmark a kernel
static double benchmark_gemm(gemm_fn fn, int M, int N, int K, int iterations) {
    float *A = (float *)malloc(M * K * sizeof(float));
    float *B = (float *)malloc(K * N * sizeof(float));
    float *C = (float *)malloc(M * N * sizeof(float));

    for (int i = 0; i < M * K; i++) A[i] = 1.0f;
    for (int i = 0; i < K * N; i++) B[i] = 2.0f;

    // Warmup
    fn(A, B, C, M, N, K);

    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    for (int i = 0; i < iterations; i++) {
        fn(A, B, C, M, N, K);
    }

    clock_gettime(CLOCK_MONOTONIC, &end);

    free(A);
    free(B);
    free(C);

    double time_spent = (end.tv_sec - start.tv_sec) + 
                        (end.tv_nsec - start.tv_nsec) / 1e9;
    return (time_spent / iterations) * 1000.0; // in milliseconds
}

int main(void) {
    printf("=====================================================\n");
    printf("=== KubePatch Daemon: Dynamic Symbol Registry Test ===\n");
    printf("=====================================================\n\n");

    symboltable symtab;
    symtab_init(&symtab);

    // -------------------------------------------------------------
    // STAGE 1: Load Generic WEAK Kernel (gemm_fallback.o)
    // -------------------------------------------------------------
    printf("[1] Loading Baseline Kernel: kernels/gemm_fallback.o ...\n");
    objectbinary obj_fallback;
    if (load_macho_object("kernels/gemm_fallback.o", &obj_fallback) != 0) {
        fprintf(stderr, "[-] Failed to load fallback kernel\n");
        return 1;
    }

    void *exec_fallback = create_exceutable_code(
        obj_fallback.file_bytes + obj_fallback.text_offset, 
        obj_fallback.text_size
    );

    uint64_t fallback_offset = 0;
    symbolbinding fallback_bind;
    find_symbol_offset(&obj_fallback, "_gemm_compute", &fallback_offset, &fallback_bind);

    void *fallback_addr = (uint8_t *)exec_fallback + fallback_offset;
    symtab_register(&symtab, "_gemm_compute", fallback_addr, fallback_bind);
    symtab_print(&symtab);

    // Execute through the registry
    gemm_fn current_gemm = (gemm_fn)symtab_lookup(&symtab, "_gemm_compute");
    printf("[*] Benchmarking Baseline (WEAK) Kernel (Matrix 128x128)...\n");
    double time_weak = benchmark_gemm(current_gemm, 128, 128, 128, 500);
    printf("[*] Baseline latency: %.4f ms per call\n\n", time_weak);

    // -------------------------------------------------------------
    // STAGE 2: Live Hot-Load Optimized STRONG Kernel (gemm_opt.o)
    // -------------------------------------------------------------
    printf("[2] Hot-loading Optimized Kernel: kernels/gemm_opt.o (SIMD / sse2neon)...\n");
    objectbinary obj_opt;
    if (load_macho_object("kernels/gemm_opt.o", &obj_opt) != 0) {
        fprintf(stderr, "[-] Failed to load optimized kernel\n");
        return 1;
    }

    void *exec_opt = create_exceutable_code(
        obj_opt.file_bytes + obj_opt.text_offset, 
        obj_opt.text_size
    );

    uint64_t opt_offset = 0;
    symbolbinding opt_bind;
    find_symbol_offset(&obj_opt, "_gemm_compute", &opt_offset, &opt_bind);

    void *opt_addr = (uint8_t *)exec_opt + opt_offset;
    // Registering will trigger the UPGRADE rule!
    symtab_register(&symtab, "_gemm_compute", opt_addr, opt_bind);
    symtab_print(&symtab);

    // Execute again through the registry (ZERO restart, instantaneous hot-swap!)
    current_gemm = (gemm_fn)symtab_lookup(&symtab, "_gemm_compute");
    printf("[*] Benchmarking Upgraded (STRONG) Kernel (Matrix 128x128)...\n");
    double time_strong = benchmark_gemm(current_gemm, 128, 128, 128, 500);
    printf("[*] Optimized latency: %.4f ms per call\n", time_strong);
    printf("[*] Speedup: %.2fx faster!\n\n", time_weak / time_strong);

    // Cleanup
    free_executable_code(exec_fallback, obj_fallback.text_size);
    free_macho_object(&obj_fallback);
    free_executable_code(exec_opt, obj_opt.text_size);
    free_macho_object(&obj_opt);

    printf("=== Test Passed: Dynamic In-Memory Hot-Relink Complete! ===\n");
    return 0;
}