CC = clang
CFLAGS = -Wall -Wextra -O2 -Iinclude

SRCS = src/core/main.c src/linker/macho_loader.c src/linker/patcher.c
OBJS = $(SRCS:.c=.o)
TARGET = kubepatch_daemon

all: $(TARGET) kernels/kernel.o

$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $(TARGET)

kernels/kernel.o: kernels/kernel.c
	$(CC) -c -O2 kernels/kernel.c -o kernels/kernel.o

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET) kernels/*.o

.PHONY: all clean