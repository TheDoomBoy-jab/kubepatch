#ifndef SYMBOL_TABLE_H
#define SYMBOL_TABLE_H

#define MAX_SYMBOLS 256
#define MAX_SYM_NAME 64

#include <stdint.h>

#include <stddef.h>

typedef enum{
SYM_WEAK,
SYM_STRONG
}symbolbinding;

typedef struct{
    char name[MAX_SYM_NAME];
    void *address;
    symbolbinding binding;
} symbolentry;

typedef struct{
    symbolentry entries[MAX_SYMBOLS];
    size_t count;
}symboltable;

void symtab_init(symboltable *table);

int symtab_register(symboltable *table, const char *name, void *address, symbolbinding binding );

void *symtab_lookup(const symboltable *table, const char *name);

void symtab_print(const symboltable *table);

#endif

