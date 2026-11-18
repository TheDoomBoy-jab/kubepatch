CC = clang
CPPFLAGS += -Iinclude
CFLAGS += -Wall -Wextra -std=c11 -O3 -pthread
CFLAGS += -D_DARWIN_C_SOURCE
LDLIBS += -pthread

COMMON = src/core/ringbuffer.o src/core/workload.o
LOADER = src/linker/macho_loader.o src/linker/patcher.o src/linker/symbol_table.o src/linker/kernel.o
SHM = src/core/shm_context.o
PROGRAMS = kubepatch_daemon pod_client bench_cluster
KERNELS = kernels/gemm_opt.o kernels/kernel.o kernels/gemm_fallback.o
OBJECTS = $(COMMON) $(LOADER) $(SHM) $(KERNELS) src/core/daemon.o src/client/pod_client.o benchmarks/bench_cluster.o tests/runtime_test.o

all: $(PROGRAMS) $(KERNELS)

kubepatch_daemon: src/core/daemon.o $(COMMON) $(LOADER) $(SHM) | $(KERNELS)
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

pod_client: src/client/pod_client.o $(COMMON) $(SHM)
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

bench_cluster: benchmarks/bench_cluster.o $(COMMON) $(LOADER) | $(KERNELS)
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

tests/runtime_test: tests/runtime_test.o $(COMMON) $(LOADER) | $(KERNELS)
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

# The small loader copies text only. Avoid compiler-generated calls to bzero
# in GEMM and reject any remaining text relocation when loading an object.
kernels/gemm_opt.o kernels/gemm_fallback.o: CFLAGS += -fno-builtin
kernels/kernel.o: CFLAGS += -fno-math-errno

%.o: %.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

-include $(OBJECTS:.o=.d)

test: all tests/runtime_test
	./tests/runtime_test
	./bench_cluster mixed
	./bench_cluster fallback
	./bench_cluster simd
	./tests/local_ipc.sh

clean:
	rm -f $(OBJECTS) $(OBJECTS:.o=.d) $(PROGRAMS) tests/runtime_test

.PHONY: all clean test
