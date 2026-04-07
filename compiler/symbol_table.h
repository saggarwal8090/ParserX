#ifndef SYMBOL_TABLE_H
#define SYMBOL_TABLE_H

#include "ast.h"
#include "json_output.h"

#define MAX_SYMBOLS_ST 256
#define SYM_NAME_LEN   128
#define SYM_TYPE_LEN   64
#define SYM_SCOPE_LEN  128

typedef struct {
    char name[SYM_NAME_LEN];
    char type[SYM_TYPE_LEN];
    char scope[SYM_SCOPE_LEN];
    int  line;
    int  references;
} SymEntry;

typedef struct {
    SymEntry entries[MAX_SYMBOLS_ST];
    int      count;
} SymTable;

/* Walk AST and build symbol table */
SymTable symbol_table_build(const ASTNode *root);

/* Serialize symbol table to JSON array */
void symbol_table_to_json(const SymTable *st, StrBuf *out);

#endif /* SYMBOL_TABLE_H */
