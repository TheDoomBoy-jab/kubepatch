#ifndef SYMBOL_TABLE_H
#define SYMBOL_TABLE_H

#include <pthread.h>
#include <stdatomic.h>
#include <stddef.h>

#define MAX_SYMBOLS 256
#define MAX_SYM_NAME 64

typedef enum { SYM_WEAK, SYM_STRONG } symbolbinding;

typedef struct {
    char name[MAX_SYM_NAME];
    _Atomic(void *) address;
    symbolbinding binding;
} symbolentry;

typedef struct {
    symbolentry entries[MAX_SYMBOLS];
    atomic_size_t count;
    pthread_mutex_t writer;
} symboltable;

int symtab_init(symboltable *table);
void symtab_destroy(symboltable *table);
int symtab_register(symboltable *table, const char *name, void *address, symbolbinding binding);
void *symtab_lookup(const symboltable *table, const char *name);

#endif
