#ifndef PARSE_TABLE_H
#define PARSE_TABLE_H

#include "json_output.h"

#define MAX_PRODUCTIONS 128
#define MAX_PT_ENTRIES  1024
#define MAX_SYM_LEN     64
#define MAX_PROD_LEN    512
#define MAX_SYMBOLS     32   /* max symbols in one production RHS */

/* ── Grammar production ─────────────── */
typedef struct {
    char lhs[MAX_SYM_LEN];
    char rhs[MAX_SYMBOLS][MAX_SYM_LEN]; /* symbols in RHS; "EPS" = epsilon */
    int  rhs_len;
} Production;

/* ── Parse table entry ──────────────── */
typedef struct {
    char nt[MAX_SYM_LEN];
    char terminal[MAX_SYM_LEN];
    char production[MAX_PROD_LEN]; /* e.g. "Program → StatementList" */
} PTEntry;

typedef struct {
    PTEntry entries[MAX_PT_ENTRIES];
    int     count;
} ParseTable;

/* Build the TinyLang parse table (FIRST/FOLLOW computed internally). */
ParseTable parse_table_build(void);

/* Look up an entry; returns production string or NULL. */
const char *parse_table_lookup(const ParseTable *pt, const char *nt, const char *terminal);

/* Serialize parse table to JSON nested object { nt: { terminal: "prod" } } */
void parse_table_to_json(const ParseTable *pt, StrBuf *out);

#endif /* PARSE_TABLE_H */
