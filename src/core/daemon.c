#include "shm_context.h"

#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <sys/mman.h>
#include <unistd.h>

static volatile sig_atomic_t stopping;

static void handle_stop(int signal_number) {
    (void)signal_number;
    stopping = 1;
}

int main(void) {
    struct sigaction action = {.sa_handler = handle_stop};
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGINT, &action, NULL) != 0 || sigaction(SIGTERM, &action, NULL) != 0) {
        perror("sigaction");
        return 1;
    }

    kernel_image gemm = {0}, layernorm = {0}, relu = {0};
    int result = 1;
    if (kernel_load("kernels/gemm_opt.o", GEMM_SYMBOL, &gemm) ||
        kernel_load("kernels/kernel.o", "_layernorm", &layernorm) ||
        kernel_load("kernels/kernel.o", "_vector_relu", &relu)) {
        goto unload;
    }

    const char *name = shm_context_name();
    int fd = shm_open(name, O_CREAT | O_EXCL | O_RDWR, 0600);
    if (fd < 0) {
        perror(name);
        goto unload;
    }

    ShmContext *shm = MAP_FAILED;
    if (ftruncate(fd, sizeof(*shm)) == 0) {
        shm = mmap(NULL, sizeof(*shm), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    }
    close(fd);
    if (shm == MAP_FAILED) {
        perror("shared memory");
        goto unlink;
    }

    ringbuffer_init(&shm->ring);
    work_slots_init(shm->slots);
    atomic_store_explicit(&shm->ready, SHM_ABI, memory_order_release);
    printf("KubePatch daemon ready on %s\n", name);
    fflush(stdout);

    kernel_dispatch dispatch = {
        .gemm = (gemm_fn)gemm.entry,
        .layernorm = (layernorm_fn)layernorm.entry,
        .relu = (relu_fn)relu.entry,
    };
    size_t completed = 0;
    size_t failed = 0;
    while (!stopping) {
        taskdescriptor task;
        if (!ringbuffer_dequeue(&shm->ring, &task)) {
            poll_pause();
            continue;
        }
        if (work_execute(shm->slots, &task, dispatch)) {
            ++completed;
        } else {
            ++failed;
        }
    }
    atomic_store_explicit(&shm->ready, 0, memory_order_release);
    munmap(shm, sizeof(*shm));
    printf("Daemon stopped. Executed %zu tasks, rejected %zu.\n", completed, failed);
    result = failed ? 1 : 0;

unlink:
    if (shm_unlink(name) != 0) {
        perror("shm_unlink");
        result = 1;
    }
unload:
    kernel_unload(&relu);
    kernel_unload(&layernorm);
    kernel_unload(&gemm);
    return result;
}
