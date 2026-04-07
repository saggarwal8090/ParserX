#include "optimizer.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* ── Line splitting ─────────────────────────── */
#define MAX_LINES 65536
static char *g_lines[MAX_LINES];         /* pointers into modified copy */
static char  g_line_buf[1024 * 1024];    /* working buffer for opt lines — matches MAX_INPUT */
static int   g_line_count;

static void init_lines(const char *src) {
    strncpy(g_line_buf, src, sizeof(g_line_buf) - 1);
    g_line_buf[sizeof(g_line_buf) - 1] = '\0';
    g_line_count = 0;
    char *p = g_line_buf;
    g_lines[g_line_count++] = p;
    while (*p && g_line_count < MAX_LINES) {
        if (*p == '\n') {
            *p = '\0';
            g_lines[g_line_count++] = p + 1;
        }
        p++;
    }
}

static void join_lines(char *out, int outlen) {
    out[0] = '\0';
    for (int i = 0; i < g_line_count; i++) {
        if (g_lines[i] == NULL) continue;
        strncat(out, g_lines[i], (size_t)outlen - strlen(out) - 1);
        if (i < g_line_count - 1)
            strncat(out, "\n", (size_t)outlen - strlen(out) - 1);
    }
}

/* ── Helper: try evaluate a BinaryOp node ───── */
static int try_eval(const ASTNode *node, double *result) {
    if (!node || strcmp(node->type, "BinaryOp") != 0) return 0;
    if (node->child_count < 2) return 0;
    const ASTNode *L = node->children[0];
    const ASTNode *R = node->children[1];
    if (strcmp(L->type, "Literal") != 0 || strcmp(R->type, "Literal") != 0) return 0;
    /* Check not string */
    if (L->value[0] == '"' || R->value[0] == '"') return 0;
    char *ep = NULL;
    double lv = strtod(L->value, &ep); if (ep == L->value) return 0;
    double rv = strtod(R->value, &ep); if (ep == R->value) return 0;
    const char *op = node->value;
    if (strcmp(op, "+") == 0) { *result = lv + rv; return 1; }
    if (strcmp(op, "-") == 0) { *result = lv - rv; return 1; }
    if (strcmp(op, "*") == 0) { *result = lv * rv; return 1; }
    if (strcmp(op, "/") == 0) {
        if (rv == 0.0) return 0;
        *result = lv / rv; return 1;
    }
    return 0;
}

static const char *op_name(const char *op) {
    if (strcmp(op, "+") == 0) return "addition";
    if (strcmp(op, "-") == 0) return "subtraction";
    if (strcmp(op, "*") == 0) return "multiplication";
    if (strcmp(op, "/") == 0) return "division";
    return "operation";
}

/* ── Constant folding ───────────────────────── */
static OptResult *g_opt;

static void const_fold(const ASTNode *node) {
    if (!node) return;
    if (strcmp(node->type, "VarDecl") == 0 && node->child_count == 1) {
        const ASTNode *expr = node->children[0];
        double val = 0.0;
        if (try_eval(expr, &val) && g_opt->opt_count < MAX_OPTS) {
            int lidx = node->line - 1;
            if (lidx >= 0 && lidx < g_line_count && g_lines[lidx]) {
                char result_str[64];
                if (val == (double)(int)val)
                    snprintf(result_str, sizeof(result_str), "%d", (int)val);
                else
                    snprintf(result_str, sizeof(result_str), "%g", val);

                /* Build new line */
                char vtype[64] = "", vname[64] = "";
                sscanf(node->value, "%63s %63s", vtype, vname);
                char new_line[512];
                /* Preserve leading whitespace */
                const char *orig = g_lines[lidx];
                int lead = 0;
                while (orig[lead] == ' ' || orig[lead] == '\t') lead++;
                snprintf(new_line, sizeof(new_line), "%.*s%s %s = %s;",
                         lead, orig, vtype, vname, result_str);

                OptEntry *oe = &g_opt->opts[g_opt->opt_count++];
                strncpy(oe->type, "Constant Folding", sizeof(oe->type) - 1);
                oe->line = node->line;
                strncpy(oe->original,  orig, OPT_STR_LEN - 1);
                strncpy(oe->optimized, new_line, OPT_STR_LEN - 1);
                const char *lv = expr->child_count > 0 ? expr->children[0]->value : "?";
                const char *rv = expr->child_count > 1 ? expr->children[1]->value : "?";
                snprintf(oe->explanation, OPT_STR_LEN,
                         "The expression %s %s %s is evaluated at compile time to produce %s, "
                         "eliminating the %s at runtime.",
                         lv, expr->value, rv, result_str, op_name(expr->value));

                g_lines[lidx] = NULL; /* temporary: will replace */
                /* We store new line in a static buffer pool — use opt entry */
                g_lines[lidx] = oe->optimized;
            }
        }
    }
    for (int i = 0; i < node->child_count; i++) const_fold(node->children[i]);
}

/* ── Dead code elimination ──────────────────── */
static void dead_code_elim(const ASTNode *node) {
    if (!node) return;
    if (strcmp(node->type, "Block") == 0) {
        int found_ret = 0, ret_line = 0;
        for (int i = 0; i < node->child_count && g_opt->opt_count < MAX_OPTS; i++) {
            if (found_ret) {
                int dead_line = node->children[i]->line;
                if (dead_line > 0) {
                    int lidx = dead_line - 1;
                    if (lidx >= 0 && lidx < g_line_count && g_lines[lidx]) {
                        OptEntry *oe = &g_opt->opts[g_opt->opt_count++];
                        strncpy(oe->type, "Dead Code Elimination", sizeof(oe->type) - 1);
                        oe->line = dead_line;
                        strncpy(oe->original,  g_lines[lidx], OPT_STR_LEN - 1);
                        strncpy(oe->optimized, "(removed)",    OPT_STR_LEN - 1);
                        snprintf(oe->explanation, OPT_STR_LEN,
                                 "This statement on line %d appears after the return statement on "
                                 "line %d and will never execute. Removing it reduces code size.",
                                 dead_line, ret_line);
                        g_lines[lidx] = NULL; /* mark as removed */
                    }
                }
            }
            if (strcmp(node->children[i]->type, "ReturnStmt") == 0) {
                found_ret = 1;
                ret_line  = node->children[i]->line;
            }
        }
    }
    for (int i = 0; i < node->child_count; i++) dead_code_elim(node->children[i]);
}

/* ── Unused variable detection ──────────────── */
/* collect declarations */
static void collect_decls(const ASTNode *node, char names[64][64], int lines[64], int *cnt) {
    if (!node) return;
    if (strcmp(node->type, "VarDecl") == 0) {
        char vtype[64] = "", vname[64] = "";
        sscanf(node->value, "%63s %63s", vtype, vname);
        if (*cnt < 64) {
            strncpy(names[*cnt], vname, 63); names[*cnt][63] = '\0';
            lines[*cnt] = node->line;
            (*cnt)++;
        }
    }
    for (int i = 0; i < node->child_count; i++) collect_decls(node->children[i], names, lines, cnt);
}

static void collect_used(const ASTNode *node, char used[64][64], int *ucnt) {
    if (!node) return;
    if (strcmp(node->type, "Identifier") == 0 || strcmp(node->type, "Assignment") == 0) {
        int found = 0;
        for (int k = 0; k < *ucnt; k++) if (strcmp(used[k], node->value) == 0) { found = 1; break; }
        if (!found && *ucnt < 64) { strncpy(used[(*ucnt)], node->value, 63); used[(*ucnt)++][63] = '\0'; }
    }
    for (int i = 0; i < node->child_count; i++) collect_used(node->children[i], used, ucnt);
}

static void unused_var_check(const ASTNode *root) {
    char decl_names[64][64]; int decl_lines[64]; int dcnt = 0;
    char used_names[64][64]; int ucnt = 0;
    collect_decls(root, decl_names, decl_lines, &dcnt);
    collect_used(root, used_names, &ucnt);
    for (int d = 0; d < dcnt && g_opt->opt_count < MAX_OPTS; d++) {
        int used = 0;
        for (int u = 0; u < ucnt; u++) if (strcmp(used_names[u], decl_names[d]) == 0) { used = 1; break; }
        if (!used) {
            OptEntry *oe = &g_opt->opts[g_opt->opt_count++];
            strncpy(oe->type, "Unused Variable", sizeof(oe->type) - 1);
            oe->line = decl_lines[d];

            /* Use the actual source line as 'original' if available */
            int lidx = decl_lines[d] - 1;
            if (lidx >= 0 && lidx < g_line_count && g_lines[lidx]) {
                strncpy(oe->original, g_lines[lidx], OPT_STR_LEN - 1);
                g_lines[lidx] = NULL;  /* ← actually remove from optimized output */
            } else {
                snprintf(oe->original, OPT_STR_LEN, "(declaration of %s)", decl_names[d]);
            }

            strncpy(oe->optimized, "(removed — unused)", OPT_STR_LEN - 1);
            snprintf(oe->explanation, OPT_STR_LEN,
                     "Variable '%s' is declared on line %d but never read anywhere in the code. "
                     "The declaration has been removed in the optimized output.",
                     decl_names[d], decl_lines[d]);
        }
    }
}


/* ── Loop invariant ─────────────────────────── */
static void expr_to_str(const ASTNode *node, char *out, int outlen) {
    if (!node || outlen <= 0) return;
    if (strcmp(node->type,"Literal")==0 || strcmp(node->type,"Identifier")==0) {
        strncpy(out, node->value, (size_t)outlen - 1); out[outlen-1] = '\0'; return;
    }
    if (strcmp(node->type,"BinaryOp")==0 && node->child_count >= 2) {
        char l[128]="", r[128]="";
        expr_to_str(node->children[0], l, sizeof(l));
        expr_to_str(node->children[1], r, sizeof(r));
        snprintf(out, (size_t)outlen, "%s %s %s", l, node->value, r);
        return;
    }
    out[0]='\0';
}

static void get_modified_vars(const ASTNode *node, char mods[64][64], int *mcnt) {
    if (!node) return;
    if (strcmp(node->type,"Assignment")==0) {
        int f=0; for(int k=0;k<*mcnt;k++) if(strcmp(mods[k],node->value)==0){f=1;break;}
        if(!f && *mcnt<64){ strncpy(mods[*mcnt],node->value,63); mods[(*mcnt)++][63]='\0'; }
    }
    if (strcmp(node->type,"VarDecl")==0) {
        char vtype[64]="",vname[64]=""; sscanf(node->value,"%63s %63s",vtype,vname);
        int f=0; for(int k=0;k<*mcnt;k++) if(strcmp(mods[k],vname)==0){f=1;break;}
        if(!f && *mcnt<64){ strncpy(mods[*mcnt],vname,63); mods[(*mcnt)++][63]='\0'; }
    }
    for(int i=0;i<node->child_count;i++) get_modified_vars(node->children[i],mods,mcnt);
}

static void get_expr_vars(const ASTNode *node, char ev[16][64], int *ecnt) {
    if (!node) return;
    if (strcmp(node->type,"Identifier")==0) {
        int f=0; for(int k=0;k<*ecnt;k++) if(strcmp(ev[k],node->value)==0){f=1;break;}
        if(!f && *ecnt<16){ strncpy(ev[*ecnt],node->value,63); ev[(*ecnt)++][63]='\0'; }
    }
    for(int i=0;i<node->child_count;i++) get_expr_vars(node->children[i],ev,ecnt);
}

static void check_invariant_exprs(const ASTNode *node,
        char mods[64][64], int mcnt) {
    if (!node) return;
    if (strcmp(node->type,"BinaryOp")==0 && g_opt->opt_count < MAX_OPTS) {
        char ev[16][64]; int ecnt=0;
        get_expr_vars(node, ev, &ecnt);
        if (ecnt > 0) {
            int modified = 0;
            for(int e=0;e<ecnt;e++)
                for(int m=0;m<mcnt;m++)
                    if(strcmp(ev[e],mods[m])==0){ modified=1; break; }
            if (!modified) {
                char estr[256]="";
                expr_to_str(node, estr, sizeof(estr));
                if (strlen(estr) > 2) {
                    OptEntry *oe = &g_opt->opts[g_opt->opt_count++];
                    strncpy(oe->type, "Loop Invariant", sizeof(oe->type)-1);
                    oe->line = node->line;
                    strncpy(oe->original,  estr, OPT_STR_LEN-1);
                    snprintf(oe->optimized, OPT_STR_LEN, "(move \"%s\" before loop)", estr);
                    snprintf(oe->explanation, OPT_STR_LEN,
                             "The expression '%s' does not depend on any variable modified inside "
                             "the loop. It can be computed once before the loop.", estr);
                    return; /* don't recurse */
                }
            }
        }
    }
    for(int i=0;i<node->child_count;i++) check_invariant_exprs(node->children[i], mods, mcnt);
}

static void loop_invariant(const ASTNode *node) {
    if (!node) return;
    if (strcmp(node->type,"WhileStmt")==0 || strcmp(node->type,"ForStmt")==0) {
        char mods[64][64]; int mcnt=0;
        get_modified_vars(node, mods, &mcnt);
        check_invariant_exprs(node, mods, mcnt);
    }
    for(int i=0;i<node->child_count;i++) loop_invariant(node->children[i]);
}

/* ── Public API ──────────────────────────────── */
OptResult optimizer_run(const ASTNode *root, const char *source_code) {
    OptResult res;
    memset(&res, 0, sizeof(res));
    if (!source_code) source_code = "";
    strncpy(res.original_code, source_code, sizeof(res.original_code) - 1);

    g_opt = &res;
    init_lines(source_code);

    const_fold(root);
    dead_code_elim(root);
    unused_var_check(root);
    loop_invariant(root);

    join_lines(res.optimized_code, sizeof(res.optimized_code));
    g_opt = NULL;
    return res;
}

void optimizer_to_json(const OptResult *res, StrBuf *out) {
    buf_append(out, "{");
    buf_append(out, "\"original_code\":");  buf_append_json_str(out, res->original_code);
    buf_append(out, ",\"optimized_code\":"); buf_append_json_str(out, res->optimized_code);
    buf_append(out, ",\"optimizations_applied\":[");
    for (int i = 0; i < res->opt_count; i++) {
        if (i > 0) buf_append(out, ",");
        const OptEntry *oe = &res->opts[i];
        buf_append(out, "{");
        buf_append(out, "\"type\":");        buf_append_json_str(out, oe->type);
        buf_appendf(out, ",\"line\":%d",     oe->line);
        buf_append(out, ",\"original\":");   buf_append_json_str(out, oe->original);
        buf_append(out, ",\"optimized\":");  buf_append_json_str(out, oe->optimized);
        buf_append(out, ",\"explanation\":"); buf_append_json_str(out, oe->explanation);
        buf_append(out, "}");
    }
    buf_append(out, "]}");
}
