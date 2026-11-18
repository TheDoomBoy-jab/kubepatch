#include "ringbuffer.h"
#include "workload.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: %s failed\n", __FILE__, __LINE__, #condition); \
        exit(1); \
    } \
} while (0)

static mpmcringbuffer ring;
static work_slot slots[WORK_SLOT_COUNT];

static void check_ring(void) {
    ringbuffer_init(&ring);
    taskdescriptor task = {0}, output;
    CHECK(!ringbuffer_dequeue(&ring, &output));
    for (size_t cycle = 0; cycle < 3; ++cycle) {
        for (size_t i = 0; i < RING_BUFFER_CAPACITY; ++i) {
            task.task_id = cycle * RING_BUFFER_CAPACITY + i;
            CHECK(ringbuffer_enqueue(&ring, &task));
        }
        CHECK(!ringbuffer_enqueue(&ring, &task));
        for (size_t i = 0; i < RING_BUFFER_CAPACITY; ++i) {
            CHECK(ringbuffer_dequeue(&ring, &output));
            CHECK(output.task_id == cycle * RING_BUFFER_CAPACITY + i);
        }
        CHECK(!ringbuffer_dequeue(&ring, &output));
    }
}

static void check_kernel(const kernel_image *image) {
    // A non-square shape and K=7 exercise both the vector loop and its tail.
    float a[] = {1, 2, 1, 0, 3, 2, 1,  -1, 0, 1, 2, 3, 4, 5};
    float b[] = {1, 0, 2,  -1, 1, 0,  2, 1, -1,  0, 1, 3,
                 1, 0, 1,  2, 2, 1,  0, 3, 1};
    float expected[] = {8, 10, 7, 12, 26, 15};
    float c[6] = {NAN, NAN, NAN, NAN, NAN, NAN};
    ((gemm_fn)image->entry)(a, b, c, 2, 3, 7);
    for (size_t i = 0; i < 6; ++i) {
        CHECK(c[i] == expected[i]);
    }
}

int main(void) {
    check_ring();

    char invalid_path[] = "/tmp/kubepatch-invalid.XXXXXX";
    int fd = mkstemp(invalid_path);
    CHECK(fd >= 0);
    char invalid_header[32] = {0};
    CHECK(write(fd, invalid_header, sizeof(invalid_header)) == sizeof(invalid_header));
    CHECK(close(fd) == 0);
    objectbinary object;
    int rejected = load_macho_object(invalid_path, &object);
    CHECK(unlink(invalid_path) == 0);
    CHECK(rejected != 0 && object.file_bytes == NULL);

    kernel_image weak, strong;
    CHECK(kernel_load("kernels/gemm_fallback.o", GEMM_SYMBOL, &weak) == 0);
    CHECK(kernel_load("kernels/gemm_opt.o", GEMM_SYMBOL, &strong) == 0);
    CHECK(weak.binding == SYM_WEAK && strong.binding == SYM_STRONG);
    check_kernel(&weak);
    check_kernel(&strong);

    symboltable table;
    CHECK(symtab_init(&table) == 0);
    CHECK(symtab_lookup(&table, GEMM_SYMBOL) == NULL);
    CHECK(symtab_register(&table, GEMM_SYMBOL, weak.entry, weak.binding) == 0);
    CHECK(symtab_lookup(&table, GEMM_SYMBOL) == weak.entry);
    CHECK(symtab_register(&table, GEMM_SYMBOL, strong.entry, strong.binding) == 0);
    CHECK(symtab_lookup(&table, GEMM_SYMBOL) == strong.entry);
    CHECK(symtab_register(&table, GEMM_SYMBOL, weak.entry, weak.binding) == 0);
    CHECK(symtab_register(&table, GEMM_SYMBOL, strong.entry, strong.binding) != 0);
    CHECK(symtab_lookup(&table, GEMM_SYMBOL) == strong.entry);

    work_slots_init(slots);
    taskdescriptor job = work_prepare(slots, 0, 10, 1, KERNEL_GEMM);
    taskdescriptor invalid = job;
    ++invalid.output_buffer_offset;
    kernel_dispatch dispatch = {.gemm = (gemm_fn)strong.entry};
    CHECK(!work_execute(slots, &invalid, dispatch));
    CHECK(work_execute(slots, &job, dispatch));
    CHECK(work_verify(slots, KERNEL_GEMM));
    CHECK(!work_execute(slots, &job, dispatch));
    job = work_prepare(slots, 0, 11, 1, KERNEL_GEMM);
    job.dim_n = MATRIX_DIM + 1;
    CHECK(!work_execute(slots, &job, dispatch));
    CHECK(!work_verify(slots, KERNEL_GEMM));

    symtab_destroy(&table);
    kernel_unload(&strong);
    kernel_unload(&weak);
    puts("Runtime checks passed");
    return 0;
}
