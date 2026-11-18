#ifndef KERNEL_H
#define KERNEL_H

#include "loader.h"

typedef void (*gemm_fn)(const float *, const float *, float *, int, int, int);
typedef void (*layernorm_fn)(const float *, float *, int, float);
typedef void (*relu_fn)(const float *, float *, int);

typedef struct {
    void *code;
    size_t size;
    void *entry;
    symbolbinding binding;
} kernel_image;

int kernel_load(const char *path, const char *symbol, kernel_image *kernel);
void kernel_unload(kernel_image *kernel);

#endif
