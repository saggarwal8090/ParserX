#include "ast.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ── AST node allocator ──────────────────────────────────── */
static ASTNode *new_node(const char *type, const char *value, int line) {
    ASTNode *n = (ASTNode *)calloc(1, sizeof(ASTNode));
    if (!n) return NULL;
    strncpy(n->type,  type,  AST_TYPE_LEN - 1);
    strncpy(n->value, value ? value : "", AST_VAL_LEN - 1);
    n->type[AST_TYPE_LEN - 1]  = '\0';
    n->value[AST_VAL_LEN - 1]  = '\0';
    n->line = line;
    n->child_count = 0;
    return n;
}

static void add_child(ASTNode *parent, ASTNode *child) {
    if (!parent || !child || parent->child_count >= MAX_CHILDREN) return;
    parent->children[parent->child_count++] = child;
}

/* ── Builder context ─────────────────────────────────────── */
typedef struct {
    const Token *tokens;
    int          count;
    int          pos;
    char         error[512];
    int          has_error;
} Builder;

static const Token *b_current(Builder *b) {
    if (b->pos < b->count) return &b->tokens[b->pos];
    return &b->tokens[b->count - 1]; /* EOF */
}

static const char *b_peek_val(Builder *b)  { return b_current(b)->value; }
static const char *b_peek_type(Builder *b) { return b_current(b)->type; }

static const Token *b_eat(Builder *b, const char *val, const char *type) {
    const Token *tok = b_current(b);
    if (val && strcmp(tok->value, val) != 0) {
        if (!b->has_error) {
            snprintf(b->error, sizeof(b->error),
                     "Expected '%s' but got '%s' on line %d", val, tok->value, tok->line);
            b->has_error = 1;
        }
        return tok;
    }
    if (type && strcmp(tok->type, type) != 0) {
        if (!b->has_error) {
            snprintf(b->error, sizeof(b->error),
                     "Expected type %s but got %s on line %d", type, tok->type, tok->line);
            b->has_error = 1;
        }
        return tok;
    }
    b->pos++;
    return tok;
}

/* Forward declarations */
static ASTNode *parse_stmt_list(Builder *b);
static ASTNode *parse_statement(Builder *b);
static ASTNode *parse_declaration(Builder *b);
static ASTNode *parse_assignment(Builder *b);
static ASTNode *parse_if(Builder *b);
static ASTNode *parse_while(Builder *b);
static ASTNode *parse_for(Builder *b);
static ASTNode *parse_return(Builder *b);
static ASTNode *parse_print(Builder *b);
static ASTNode *parse_func_decl(Builder *b);
static ASTNode *parse_block(Builder *b);
static ASTNode *parse_condition(Builder *b);
static ASTNode *parse_expr(Builder *b);
static ASTNode *parse_term(Builder *b);
static ASTNode *parse_factor(Builder *b);
static ASTNode *parse_assign_no_semi(Builder *b);

/* ── Program ─────────────────────────────────────────────── */
static ASTNode *parse_program(Builder *b) {
    ASTNode *prog = new_node("Program", "", 1);
    if (!prog) return NULL;
    /* parse statement list and attach children directly */
    ASTNode *list = parse_stmt_list(b);
    if (list) {
        for (int i = 0; i < list->child_count; i++)
            add_child(prog, list->children[i]);
        list->child_count = 0;
        ast_free(list);
    }
    return prog;
}

/* ── StatementList ───────────────────────────────────────── */
static ASTNode *parse_stmt_list(Builder *b) {
    ASTNode *list = new_node("_List", "", 0);
    if (!list) return NULL;
    while (!b->has_error) {
        const char *typ = b_peek_type(b);
        const char *val = b_peek_val(b);
        if (strcmp(typ, "EOF") == 0 || strcmp(val, "}") == 0) break;
        ASTNode *stmt = parse_statement(b);
        if (stmt) add_child(list, stmt);
        else break;
    }
    return list;
}

/* ── Statement ───────────────────────────────────────────── */
static ASTNode *parse_statement(Builder *b) {
    if (b->has_error) return NULL;
    const char *val = b_peek_val(b);
    const char *typ = b_peek_type(b);

    if (strcmp(val, "int")    == 0 || strcmp(val, "float") == 0 ||
        strcmp(val, "string") == 0) return parse_declaration(b);
    if (strcmp(val, "if")     == 0) return parse_if(b);
    if (strcmp(val, "while")  == 0) return parse_while(b);
    if (strcmp(val, "for")    == 0) return parse_for(b);
    if (strcmp(val, "return") == 0) return parse_return(b);
    if (strcmp(val, "print")  == 0) return parse_print(b);
    if (strcmp(val, "func")   == 0) return parse_func_decl(b);
    if (strcmp(val, "{")      == 0) return parse_block(b);
    if (strcmp(typ, "IDENTIFIER") == 0) return parse_assignment(b);
    /* Unknown: skip token to avoid infinite loop */
    b->pos++;
    return NULL;
}

/* ── Declaration: Type ID [= Expr] ; ───────────────────────── */
static ASTNode *parse_declaration(Builder *b) {
    const Token *type_tok = b_eat(b, NULL, NULL);
    const Token *name_tok = b_eat(b, NULL, "IDENTIFIER");

    char val[AST_VAL_LEN];
    snprintf(val, sizeof(val), "%s %s", type_tok->value, name_tok->value);
    ASTNode *n = new_node("VarDecl", val, type_tok->line);

    if (strcmp(b_peek_val(b), "=") == 0) {
        /* Initialised declaration:  int x = Expr ; */
        b_eat(b, "=", NULL);
        ASTNode *expr = parse_expr(b);
        b_eat(b, ";", NULL);
        if (expr) add_child(n, expr);
    } else {
        /* Uninitialised declaration: int x ; */
        b_eat(b, ";", NULL);
    }

    return n;
}

/* ── Assignment: ID = Expr ; ────────────────────────────── */
static ASTNode *parse_assignment(Builder *b) {
    const Token *name_tok = b_eat(b, NULL, "IDENTIFIER");
    b_eat(b, "=", NULL);
    ASTNode *expr = parse_expr(b);
    b_eat(b, ";", NULL);
    ASTNode *n = new_node("Assignment", name_tok->value, name_tok->line);
    if (expr) add_child(n, expr);
    return n;
}

/* ── Assignment without semicolon (for‐loop update) ─────── */
static ASTNode *parse_assign_no_semi(Builder *b) {
    const Token *name_tok = b_eat(b, NULL, "IDENTIFIER");
    b_eat(b, "=", NULL);
    ASTNode *expr = parse_expr(b);
    ASTNode *n = new_node("Assignment", name_tok->value, name_tok->line);
    if (expr) add_child(n, expr);
    return n;
}

/* ── IfStmt ──────────────────────────────────────────────── */
static ASTNode *parse_if(Builder *b) {
    const Token *tok = b_eat(b, "if", NULL);
    b_eat(b, "(", NULL);
    ASTNode *cond = parse_condition(b);
    b_eat(b, ")", NULL);
    ASTNode *block = parse_block(b);
    ASTNode *n = new_node("IfStmt", "if", tok->line);
    if (cond) add_child(n, cond);
    if (block) add_child(n, block);
    if (strcmp(b_peek_val(b), "else") == 0) {
        b_eat(b, "else", NULL);
        ASTNode *eb = parse_block(b);
        if (eb) add_child(n, eb);
    }
    return n;
}

/* ── WhileStmt ───────────────────────────────────────────── */
static ASTNode *parse_while(Builder *b) {
    const Token *tok = b_eat(b, "while", NULL);
    b_eat(b, "(", NULL);
    ASTNode *cond = parse_condition(b);
    b_eat(b, ")", NULL);
    ASTNode *block = parse_block(b);
    ASTNode *n = new_node("WhileStmt", "while", tok->line);
    if (cond) add_child(n, cond);
    if (block) add_child(n, block);
    return n;
}

/* ── ForStmt ─────────────────────────────────────────────── */
static ASTNode *parse_for(Builder *b) {
    const Token *tok = b_eat(b, "for", NULL);
    b_eat(b, "(", NULL);
    ASTNode *init = parse_assignment(b);
    ASTNode *cond = parse_condition(b);
    b_eat(b, ";", NULL);
    ASTNode *update = parse_assign_no_semi(b);
    b_eat(b, ")", NULL);
    ASTNode *block = parse_block(b);
    ASTNode *n = new_node("ForStmt", "for", tok->line);
    if (init)   add_child(n, init);
    if (cond)   add_child(n, cond);
    if (update) add_child(n, update);
    if (block)  add_child(n, block);
    return n;
}

/* ── ReturnStmt ──────────────────────────────────────────── */
static ASTNode *parse_return(Builder *b) {
    const Token *tok = b_eat(b, "return", NULL);
    ASTNode *expr = parse_expr(b);
    b_eat(b, ";", NULL);
    ASTNode *n = new_node("ReturnStmt", "return", tok->line);
    if (expr) add_child(n, expr);
    return n;
}

/* ── PrintStmt ───────────────────────────────────────────── */
static ASTNode *parse_print(Builder *b) {
    const Token *tok = b_eat(b, "print", NULL);
    b_eat(b, "(", NULL);
    ASTNode *expr = parse_expr(b);
    b_eat(b, ")", NULL);
    b_eat(b, ";", NULL);
    ASTNode *n = new_node("PrintStmt", "print", tok->line);
    if (expr) add_child(n, expr);
    return n;
}

/* ── FuncDecl ────────────────────────────────────────────── */
static ASTNode *parse_func_decl(Builder *b) {
    const Token *tok = b_eat(b, "func", NULL);
    const Token *name_tok = b_eat(b, NULL, "IDENTIFIER");
    b_eat(b, "(", NULL);
    ASTNode *n = new_node("FuncDecl", name_tok->value, tok->line);
    /* Param list */
    while (strcmp(b_peek_val(b), "int")    == 0 ||
           strcmp(b_peek_val(b), "float")  == 0 ||
           strcmp(b_peek_val(b), "string") == 0) {
        const Token *ptyp = b_eat(b, NULL, NULL);
        const Token *pnam = b_eat(b, NULL, "IDENTIFIER");
        char pval[AST_VAL_LEN];
        snprintf(pval, sizeof(pval), "%s %s", ptyp->value, pnam->value);
        ASTNode *param = new_node("Param", pval, ptyp->line);
        add_child(n, param);
        if (strcmp(b_peek_val(b), ",") == 0) b_eat(b, ",", NULL);
        else break;
    }
    b_eat(b, ")", NULL);
    ASTNode *blk = parse_block(b);
    if (blk) add_child(n, blk);
    return n;
}

/* ── Block ───────────────────────────────────────────────── */
static ASTNode *parse_block(Builder *b) {
    const Token *tok = b_eat(b, "{", NULL);
    ASTNode *list = parse_stmt_list(b);
    b_eat(b, "}", NULL);
    ASTNode *n = new_node("Block", "", tok->line);
    if (list) {
        for (int i = 0; i < list->child_count; i++)
            add_child(n, list->children[i]);
        list->child_count = 0;
        ast_free(list);
    }
    return n;
}

/* ── Condition ───────────────────────────────────────────── */
static ASTNode *parse_condition(Builder *b) {
    ASTNode *left = parse_expr(b);
    const char *v = b_peek_val(b);
    if (strcmp(v,"==")==0 || strcmp(v,"!=")==0 || strcmp(v,"<")==0 ||
        strcmp(v,">")==0  || strcmp(v,"<=")==0 || strcmp(v,">=")==0) {
        const Token *op = b_eat(b, NULL, NULL);
        ASTNode *right = parse_expr(b);
        ASTNode *n = new_node("Condition", op->value, op->line);
        if (left)  add_child(n, left);
        if (right) add_child(n, right);
        return n;
    }
    return left;
}

/* ── Expr ────────────────────────────────────────────────── */
static ASTNode *parse_expr(Builder *b) {
    ASTNode *node = parse_term(b);
    while (!b->has_error) {
        const char *v = b_peek_val(b);
        if (strcmp(v,"+") != 0 && strcmp(v,"-") != 0) break;
        const Token *op = b_eat(b, NULL, NULL);
        ASTNode *right = parse_term(b);
        ASTNode *bin = new_node("BinaryOp", op->value, op->line);
        add_child(bin, node);
        if (right) add_child(bin, right);
        node = bin;
    }
    return node;
}

/* ── Term ────────────────────────────────────────────────── */
static ASTNode *parse_term(Builder *b) {
    ASTNode *node = parse_factor(b);
    while (!b->has_error) {
        const char *v = b_peek_val(b);
        if (strcmp(v,"*") != 0 && strcmp(v,"/") != 0) break;
        const Token *op = b_eat(b, NULL, NULL);
        ASTNode *right = parse_factor(b);
        ASTNode *bin = new_node("BinaryOp", op->value, op->line);
        add_child(bin, node);
        if (right) add_child(bin, right);
        node = bin;
    }
    return node;
}

/* ── Factor ──────────────────────────────────────────────── */
static ASTNode *parse_factor(Builder *b) {
    const Token *tok = b_current(b);
    if (strcmp(tok->value, "(") == 0) {
        b_eat(b, "(", NULL);
        ASTNode *e = parse_expr(b);
        b_eat(b, ")", NULL);
        return e;
    }
    if (strcmp(tok->type, "IDENTIFIER") == 0) {
        const Token *name_tok = b_eat(b, NULL, "IDENTIFIER");
        /* Function call? */
        if (strcmp(b_peek_val(b), "(") == 0) {
            b_eat(b, "(", NULL);
            ASTNode *fcall = new_node("FuncCall", name_tok->value, name_tok->line);
            if (strcmp(b_peek_val(b), ")") != 0) {
                ASTNode *arg = parse_expr(b);
                if (arg) add_child(fcall, arg);
                while (strcmp(b_peek_val(b), ",") == 0) {
                    b_eat(b, ",", NULL);
                    ASTNode *a2 = parse_expr(b);
                    if (a2) add_child(fcall, a2);
                }
            }
            b_eat(b, ")", NULL);
            return fcall;
        }
        return new_node("Identifier", name_tok->value, name_tok->line);
    }
    if (strcmp(tok->type, "INTEGER") == 0) {
        b_eat(b, NULL, NULL);
        return new_node("Literal", tok->value, tok->line);
    }
    if (strcmp(tok->type, "FLOAT") == 0) {
        b_eat(b, NULL, NULL);
        return new_node("Literal", tok->value, tok->line);
    }
    if (strcmp(tok->type, "STRING") == 0) {
        b_eat(b, NULL, NULL);
        char sv[AST_VAL_LEN];
        snprintf(sv, sizeof(sv), "\"%s\"", tok->value);
        return new_node("Literal", sv, tok->line);
    }
    if (!b->has_error) {
        snprintf(b->error, sizeof(b->error),
                 "Unexpected token '%s' on line %d", tok->value, tok->line);
        b->has_error = 1;
    }
    return NULL;
}

/* ── Public API ──────────────────────────────────────────── */
ASTResult ast_build(const Token *tokens, int count) {
    ASTResult res;
    res.root = NULL;
    res.has_error = 0;
    res.error[0] = '\0';

    if (!tokens || count == 0) {
        snprintf(res.error, sizeof(res.error), "No tokens");
        res.has_error = 1;
        return res;
    }

    Builder b;
    b.tokens    = tokens;
    b.count     = count;
    b.pos       = 0;
    b.has_error = 0;
    b.error[0]  = '\0';

    res.root = parse_program(&b);
    if (b.has_error) {
        res.has_error = 1;
        strncpy(res.error, b.error, sizeof(res.error) - 1);
        res.error[sizeof(res.error) - 1] = '\0';
    }
    return res;
}

void ast_to_json(const ASTNode *node, StrBuf *out) {
    if (!node) { buf_append(out, "null"); return; }
    buf_append(out, "{");
    buf_append(out, "\"type\":");  buf_append_json_str(out, node->type);
    buf_append(out, ",\"value\":"); buf_append_json_str(out, node->value);
    buf_appendf(out, ",\"line\":%d", node->line);
    buf_append(out, ",\"children\":[");
    for (int i = 0; i < node->child_count; i++) {
        if (i > 0) buf_append(out, ",");
        ast_to_json(node->children[i], out);
    }
    buf_append(out, "]}");
}

void ast_free(ASTNode *node) {
    if (!node) return;
    for (int i = 0; i < node->child_count; i++)
        ast_free(node->children[i]);
    free(node);
}
