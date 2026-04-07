#ifndef AST_H
#define AST_H

#include "lexer.h"
#include "json_output.h"

#define MAX_CHILDREN 64
#define AST_TYPE_LEN 64
#define AST_VAL_LEN  512

typedef struct ASTNode {
    char type[AST_TYPE_LEN];
    char value[AST_VAL_LEN];
    int  line;
    struct ASTNode *children[MAX_CHILDREN];
    int child_count;
} ASTNode;

typedef struct {
    ASTNode *root;    /* NULL on error */
    char error[512];
    int  has_error;
} ASTResult;

/* Build AST from token array. */
ASTResult ast_build(const Token *tokens, int count);

/* Serialize AST to JSON. */
void ast_to_json(const ASTNode *node, StrBuf *out);

/* Free the entire AST tree. */
void ast_free(ASTNode *node);

#endif /* AST_H */
