#include "symbol_table.h"
#include <stdio.h>
#include <string.h>

void symtab_init(symboltable *table) {
    if (!table) return;
    table->count = 0;
    memset(table->entries, 0, sizeof(table->entries));
}

int symtab_register(symboltable *table, const char *name, void *address, symbolbinding binding) {
    if (!table || !name || !address) return -1;


    for (size_t i = 0; i < table->count; i++) {
        if (strcmp(table->entries[i].name, name) == 0) {
            // Found existing symbol with the same name! Apply Linker Precedence:


            if (table->entries[i].binding == SYM_WEAK && binding == SYM_STRONG) {
                printf("[SymTab] UPGRADING weak symbol '%s' (%p) -> strong symbol (%p)\n",
                       name, table->entries[i].address, address);
                table->entries[i].address = address;
                table->entries[i].binding = SYM_STRONG;
                return 0;
            }


            if (table->entries[i].binding == SYM_STRONG && binding == SYM_WEAK) {
                printf("[SymTab] IGNORING incoming weak symbol '%s' (strong already bound at %p)\n",
                       name, table->entries[i].address);
                return 0;
            }


            if (table->entries[i].binding == SYM_STRONG && binding == SYM_STRONG) {
                fprintf(stderr, "[-] Linker Error: Duplicate strong symbol '%s' collision!\n", name);
                return -1;
            }


            return 0;
        }
    }


    if (table->count >= MAX_SYMBOLS) {
        fprintf(stderr, "[-] Error: Symbol table capacity reached (%d)\n", MAX_SYMBOLS);
        return -1;
    }

    strncpy(table->entries[table->count].name, name, MAX_SYM_NAME - 1);
    table->entries[table->count].address = address;
    table->entries[table->count].binding = binding;
    table->count++;

    printf("[SymTab] Registered '%s' as %s at %p\n", 
           name, binding == SYM_STRONG ? "STRONG" : "WEAK", address);
    return 0;
}

void *symtab_lookup(const symboltable *table, const char *name) {
    if (!table || !name) return NULL;
    for (size_t i = 0; i < table->count; i++) {
        if (strcmp(table->entries[i].name, name) == 0) {
            return table->entries[i].address;
        }
    }
    return NULL;
}

void symtab_print(const symboltable *table) {
    if (!table) return;
    printf("\n=== Current Symbol Registry (%zu symbols) ===\n", table->count);
    for (size_t i = 0; i < table->count; i++) {
        printf("  [%zu] %-20s %-8s -> %p\n", 
               i,
               table->entries[i].name,
               table->entries[i].binding == SYM_STRONG ? "[STRONG]" : "[WEAK]",
               table->entries[i].address);
    }
    printf("=============================================\n\n");
}