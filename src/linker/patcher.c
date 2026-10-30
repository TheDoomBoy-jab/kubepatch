#include "patcher.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <libkern/OSCacheControl.h>

void *create_exceutable_code(const uint8_t *code_bytes, size_t code_size){

    if(!code_bytes || code_size == 0)return NULL;

    void *exec_mem = mmap(NULL, code_size, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    if(exec_mem == MAP_FAILED){
        perror("[-] mmap exec_mem failed");
        return NULL;
    }

    memcpy(exec_mem, code_bytes, code_size);

    if (mprotect(exec_mem, code_size, PROT_READ | PROT_EXEC) != 0) {
        perror("[-] mprotect PROT_READ|PROT_EXEC failed");
        munmap(exec_mem, code_size);
        return NULL;
    }

    sys_icache_invalidate(exec_mem, code_size);
    return exec_mem;


}

void free_executable_code(void *addr, size_t code_size) {
    if (addr && addr != MAP_FAILED) {
        munmap(addr, code_size);
    }
}