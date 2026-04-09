#include "parser.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* non-terminal set (must stay in sync with parse_table.c) */
static const char *NON_TERMINALS[] = {
    "Program","StatementList","Statement","Declaration","DeclTail","Type","Assignment",
    "IfStmt","ElsePart","WhileStmt","ForStmt","ReturnStmt","PrintStmt",
    "FuncDecl","ParamList","MoreParams","Block","Condition","RelOp",
    "Expr","ExprRest","Term","TermRest","Factor","FactorRest","ArgList","MoreArgs",
    "ForAssign", "ForInit", NULL
};

static int is_nonterminal(const char *sym) {
    for (int i = 0; NON_TERMINALS[i]; i++)
        if (strcmp(NON_TERMINALS[i], sym) == 0) return 1;
    return 0;
}

/* Map a token to its grammar terminal string */
static void token_to_terminal(const Token *tok, char *out, int outlen) {
    if (strcmp(tok->type, "EOF") == 0) {
        strncpy(out, "$", (size_t)outlen - 1);
    } else if (strcmp(tok->type, "KEYWORD") == 0 ||
               strcmp(tok->type, "OPERATOR") == 0 ||
               strcmp(tok->type, "DELIMITER") == 0) {
        strncpy(out, tok->value, (size_t)outlen - 1);
    } else {
        /* IDENTIFIER, INTEGER, FLOAT, STRING → use type as terminal */
        strncpy(out, tok->type, (size_t)outlen - 1);
    }
    out[outlen - 1] = '\0';
}

#define STACK_SIZE 2048

ParseResult parser_run(const Token *tokens, int count, const ParseTable *pt) {
    ParseResult res;
    res.accepted = 0;
    res.error_msg[0] = '\0';

    if (!tokens || count == 0) {
        snprintf(res.error_msg, sizeof(res.error_msg), "No tokens to parse");
        return res;
    }

    /* Stack: array of strings, top at stack[top-1] */
    char stack[STACK_SIZE][MAX_SYM_LEN];
    int  top = 0;

    /* Push $ then Program */
    strncpy(stack[top++], "$",       MAX_SYM_LEN - 1); stack[top-1][MAX_SYM_LEN-1] = '\0';
    strncpy(stack[top++], "Program", MAX_SYM_LEN - 1); stack[top-1][MAX_SYM_LEN-1] = '\0';

    int pos = 0;
    int max_steps = 5000;
    int steps = 0;

    while (top > 0 && steps++ < max_steps) {
        char *sym = stack[top - 1];
        const Token *cur = (pos < count) ? &tokens[pos]
                                         : &tokens[count - 1]; /* EOF */
        char terminal[MAX_SYM_LEN];
        token_to_terminal(cur, terminal, sizeof(terminal));

        if (strcmp(sym, "$") == 0) {
            if (strcmp(terminal, "$") == 0) {
                res.accepted = 1;
                return res;
            } else {
                snprintf(res.error_msg, sizeof(res.error_msg),
                         "Syntax error at line %d: unexpected token '%s'",
                         cur->line, cur->value);
                return res;
            }
        }

        if (!is_nonterminal(sym)) {
            /* Terminal on stack */
            if (strcmp(sym, terminal) == 0) {
                top--;
                pos++;
            } else {
                snprintf(res.error_msg, sizeof(res.error_msg),
                         "Syntax error at line %d: expected '%s' but got '%s'",
                         cur->line, sym, cur->value[0] ? cur->value : terminal);
                return res;
            }
        } else {
            /* Non-terminal: look up parse table */
            const char *prod = parse_table_lookup(pt, sym, terminal);
            if (!prod) {
                snprintf(res.error_msg, sizeof(res.error_msg),
                         "Syntax error at line %d: no rule for (%s, '%s')",
                         cur->line, sym, terminal);
                return res;
            }
            top--; /* pop the non-terminal */

            /* Parse the RHS from the production string (after → / utf8 arrow) */
            /* Format: "NT → sym1 sym2 ..." or "NT → ε" */
            const char *arrow = strstr(prod, "\xe2\x86\x92"); /* UTF-8 → */
            if (!arrow) { /* fallback: ASCII -> */
                arrow = strstr(prod, "->");
                if (arrow) arrow += 2;
            } else {
                arrow += 3; /* skip 3-byte UTF-8 arrow */
            }
            if (!arrow) continue;
            /* Skip leading space */
            while (*arrow == ' ') arrow++;

            /* Tokenize RHS symbols and push in reverse */
            char rhs_buf[MAX_PROD_LEN];
            strncpy(rhs_buf, arrow, sizeof(rhs_buf) - 1);
            rhs_buf[sizeof(rhs_buf) - 1] = '\0';

            /* Check for epsilon (UTF-8 ε = \xce\xb5) or ASCII "EPS" */
            if (strcmp(rhs_buf, "\xce\xb5") == 0 || strcmp(rhs_buf, "EPS") == 0 ||
                rhs_buf[0] == '\0') {
                /* Epsilon: push nothing */
                continue;
            }

            /* Split RHS into symbols */
            char syms[MAX_SYMBOLS][MAX_SYM_LEN];
            int  sym_count = 0;
            char *tok_ptr = rhs_buf;
            char *sp = NULL;
            while (sym_count < MAX_SYMBOLS) {
                /* Skip spaces */
                while (*tok_ptr == ' ') tok_ptr++;
                if (*tok_ptr == '\0') break;
                /* Find next space */
                sp = strchr(tok_ptr, ' ');
                int slen = sp ? (int)(sp - tok_ptr) : (int)strlen(tok_ptr);
                if (slen <= 0) break;
                if (slen >= MAX_SYM_LEN) slen = MAX_SYM_LEN - 1;
                memcpy(syms[sym_count], tok_ptr, (size_t)slen);
                syms[sym_count][slen] = '\0';
                sym_count++;
                if (!sp) break;
                tok_ptr = sp + 1;
            }

            /* Push in reverse order */
            for (int k = sym_count - 1; k >= 0; k--) {
                if (top >= STACK_SIZE) {
                    snprintf(res.error_msg, sizeof(res.error_msg), "Parse stack overflow");
                    return res;
                }
                strncpy(stack[top], syms[k], MAX_SYM_LEN - 1);
                stack[top][MAX_SYM_LEN - 1] = '\0';
                top++;
            }
        }
    }

    if (steps >= max_steps) {
        snprintf(res.error_msg, sizeof(res.error_msg), "Parsing exceeded step limit");
        return res;
    }

    res.accepted = 1;
    return res;
}
