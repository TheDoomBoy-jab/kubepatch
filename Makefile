CC = clang
CFLAGS = -Wall -Wextra -O3 -Iinclude -pthread

COMMON_SRCS = src/linker/macho_loader.c src/linker/patcher.c src/linker/symbol_table.c src/core/ringbuffer.c src/core/arena.c
COMMON_OBJS = $(COMMON_SRCS:.c=.o)

DAEMON = kubepatch_daemon
CLIENT = pod_client
KERNELS = kernels/gemm_opt.o kernels/kernel.o

all: $(DAEMON) $(CLIENT) $(KERNELS)

$(DAEMON): src/core/daemon.o $(COMMON_OBJS)
	$(CC) $(CFLAGS) $^ -o $@

$(CLIENT): src/client/pod_client.o src/core/ringbuffer.o
	$(CC) $(CFLAGS) $^ -o $@

kernels/gemm_opt.o: kernels/gemm_opt.c
	$(CC) -c -O3 -Iinclude $< -o $@

kernels/kernel.o: kernels/kernel.c
	$(CC) -c -O2 $< -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f src/core/*.o src/linker/*.o src/client/*.o $(DAEMON) $(CLIENT) kernels/*.o

.PHONY: all clean