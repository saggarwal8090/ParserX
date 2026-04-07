#ifndef OPTIMIZER_H
#define OPTIMIZER_H

#include "ast.h"
#include "json_output.h"

#define MAX_OPTS 128
#define OPT_STR_LEN 512

typedef struct {
    char type[64];
    int  line;
    char original[OPT_STR_LEN];
    char optimized[OPT_STR_LEN];
    char explanation[OPT_STR_LEN];
} OptEntry;

typedef struct {
    char      original_code[1024 * 64];  /* up to 64KB */
    char      optimized_code[1024 * 64];
    OptEntry  opts[MAX_OPTS];
    int       opt_count;
} OptResult;

/* Run optimizer on AST + source code */
OptResult optimizer_run(const ASTNode *root, const char *source_code);

/* Serialize optimizer result to JSON */
void optimizer_to_json(const OptResult *res, StrBuf *out);

#endif /* OPTIMIZER_H */
