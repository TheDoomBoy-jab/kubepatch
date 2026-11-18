#include "symbol_table.h"
#include <string.h>

int symtab_init(symboltable *table) {
    atomic_init(&table->count, 0);
    return pthread_mutex_init(&table->writer, NULL);
}

void symtab_destroy(symboltable *table) {
    pthread_mutex_destroy(&table->writer);
}

int symtab_register(symboltable *table, const char *name, void *address, symbolbinding binding) {
    if (!table || !name || !*name || strlen(name) >= MAX_SYM_NAME || !address ||
        (binding != SYM_WEAK && binding != SYM_STRONG)) {
        return -1;
    }
    if (pthread_mutex_lock(&table->writer) != 0) {
        return -1;
    }

    size_t count = atomic_load_explicit(&table->count, memory_order_relaxed);
    for (size_t i = 0; i < count; i++) {
        symbolentry *entry = &table->entries[i];
        if (strcmp(entry->name, name) != 0) {
            continue;
        }
        int result = 0;
        if (entry->binding == SYM_WEAK && binding == SYM_STRONG) {
            entry->binding = SYM_STRONG;
            atomic_store_explicit(&entry->address, address, memory_order_release);
        } else if (entry->binding == SYM_STRONG && binding == SYM_STRONG) {
            result = -1;
        }
        pthread_mutex_unlock(&table->writer);
        return result;
    }

    int result = -1;
    if (count < MAX_SYMBOLS) {
        symbolentry *entry = &table->entries[count];
        strcpy(entry->name, name);
        entry->binding = binding;
        atomic_init(&entry->address, address);
        atomic_store_explicit(&table->count, count + 1, memory_order_release);
        result = 0;
    }
    pthread_mutex_unlock(&table->writer);
    return result;
}

void *symtab_lookup(const symboltable *table, const char *name) {
    if (!table || !name) {
        return NULL;
    }
    size_t count = atomic_load_explicit(&table->count, memory_order_acquire);
    for (size_t i = 0; i < count; i++) {
        if (strcmp(table->entries[i].name, name) == 0) {
            return atomic_load_explicit(&table->entries[i].address, memory_order_acquire);
        }
    }
    return NULL;
}
