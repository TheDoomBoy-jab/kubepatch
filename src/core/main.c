#include <stdio.h>
#include <stdlib.h>
#include "loader.h"
#include "patcher.h"

typedef void (*gemm_fn)(const float*, const float*, float*, int, int, int);

typedef void (*relu_fn)(const float*, float*, int);

int main(void){
    printf("===kubepatch daemon initializing===");

    const char *kernel_path = "kernels/kernel.o";

    objectbinary obj;

    if(load_macho_object(kernel_path, &obj)!=0){
        fprintf(stderr, "[-] Failed to load %s\n", kernel_path);
        return 1;
    }

    printf("[+] Parsed %s: Text section size = %llu bytes\n", kernel_path, obj.text_size);

    uint8_t *code_start = obj.file_bytes + obj.text_offset;

    void *exec_base = create_exceutable_code(code_start, obj.text_size);
    if(!exec_base){
        fprintf(stderr,"[-] Failed to create executable memory page\n");
        free_macho_object(&obj);

        return 1;
    }

    printf("[+] executable memory ready at %p\n", exec_base);

    uint64_t gemm_offset = 0;
    uint64_t relu_offset = 0;


    if(find_symbol_offset(&obj, "_gemm_naive", &gemm_offset)==0){
        printf("[+] resolved _gemm_naive entry at +0x%llx\n", gemm_offset);

        gemm_fn run_gemm = (gemm_fn)((uint8_t *)exec_base+gemm_offset);
        

        float A[4] = {1.0f, 2.0f, 3.0f, 4.0f};
        float B[4] = {5.0f, 6.0f, 7.0f, 8.0f};
        float C[4] = {0};


        run_gemm(A, B, C, 2, 2, 2);

        printf("[+] GEMM Result C:\n");
        printf("    [ %5.1f  %5.1f ]\n", C[0], C[1]);
        printf("    [ %5.1f  %5.1f ]\n", C[2], C[3]);
    }

    if(find_symbol_offset(&obj, "_vector_relu", &relu_offset)==0){
        printf("[+] Resolved _vector_relu entry at +0x%llx\n", relu_offset);
        relu_fn run_relu = (relu_fn)((uint8_t *)exec_base + relu_offset);
        float in[4] = {-2.0f, 3.0f, -0.5f, 4.0f};
        float out[4] = {0};
        run_relu(in, out, 4);
        printf("[+] ReLU Result:\n    [ ");
        for (int i = 0; i < 4; i++) printf("%.1f ", out[i]);
        printf("]\n");
    }

    free_executable_code(exec_base, obj.text_size);
    free_macho_object(&obj);
    printf("=== daemon Execution Complete ===\n");
    return 0;


}