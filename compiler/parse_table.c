#include "parse_table.h"
#include "json_output.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* ── Grammar definition ─────────────────────────────────────── */
/* Non-terminals use CamelCase; terminals use exact string values.
   Special: "EPS" = epsilon, "IDENTIFIER","INTEGER","FLOAT","STRING" = token types */

static Production productions[MAX_PRODUCTIONS];
static int prod_count = 0;

static char non_terminals[64][MAX_SYM_LEN];
static int  nt_count = 0;

#define EPS "EPS"

static void add_prod(const char *lhs, const char *rhs[], int rhs_len) {
    if (prod_count >= MAX_PRODUCTIONS) return;
    Production *p = &productions[prod_count++];
    strncpy(p->lhs, lhs, MAX_SYM_LEN - 1); p->lhs[MAX_SYM_LEN - 1] = '\0';
    for (int i = 0; i < rhs_len && i < MAX_SYMBOLS; i++) {
        strncpy(p->rhs[i], rhs[i], MAX_SYM_LEN - 1);
        p->rhs[i][MAX_SYM_LEN - 1] = '\0';
    }
    p->rhs_len = rhs_len;
}

static void register_nt(const char *nt) {
    for (int i = 0; i < nt_count; i++)
        if (strcmp(non_terminals[i], nt) == 0) return;
    if (nt_count < 64) {
        strncpy(non_terminals[nt_count++], nt, MAX_SYM_LEN - 1);
        non_terminals[nt_count - 1][MAX_SYM_LEN - 1] = '\0';
    }
}

static int is_nt(const char *sym) {
    for (int i = 0; i < nt_count; i++)
        if (strcmp(non_terminals[i], sym) == 0) return 1;
    return 0;
}

static void init_grammar(void) {
    if (prod_count > 0) return; /* already initialised */

    /* Register non-terminals */
    const char *nts[] = {
        "Program","StatementList","Statement","Declaration","DeclTail","Type","Assignment",
        "IfStmt","ElsePart","WhileStmt","ForStmt","ReturnStmt","PrintStmt",
        "FuncDecl","ParamList","MoreParams","Block","Condition","RelOp",
        "Expr","ExprRest","Term","TermRest","Factor","FactorRest","ArgList","MoreArgs",
        "ForAssign", NULL
    };
    for (int i = 0; nts[i]; i++) register_nt(nts[i]);

    /* Productions — mirrors Python parser.py GRAMMAR */
    /* Program → StatementList */
    { const char *r[] = {"StatementList"}; add_prod("Program", r, 1); }

    /* StatementList → Statement StatementList | EPS */
    { const char *r[] = {"Statement","StatementList"}; add_prod("StatementList", r, 2); }
    { const char *r[] = {EPS}; add_prod("StatementList", r, 1); }

    /* Statement → Declaration | Assignment | IfStmt | WhileStmt | ForStmt | ReturnStmt | PrintStmt | FuncDecl | Block */
    { const char *r[] = {"Declaration"};  add_prod("Statement", r, 1); }
    { const char *r[] = {"Assignment"};   add_prod("Statement", r, 1); }
    { const char *r[] = {"IfStmt"};       add_prod("Statement", r, 1); }
    { const char *r[] = {"WhileStmt"};    add_prod("Statement", r, 1); }
    { const char *r[] = {"ForStmt"};      add_prod("Statement", r, 1); }
    { const char *r[] = {"ReturnStmt"};   add_prod("Statement", r, 1); }
    { const char *r[] = {"PrintStmt"};    add_prod("Statement", r, 1); }
    { const char *r[] = {"FuncDecl"};     add_prod("Statement", r, 1); }
    { const char *r[] = {"Block"};        add_prod("Statement", r, 1); }

    /* Declaration → Type IDENTIFIER DeclTail */
    { const char *r[] = {"Type","IDENTIFIER","DeclTail"}; add_prod("Declaration", r, 3); }

    /* DeclTail → = Expr ; | ; */
    { const char *r[] = {"=","Expr",";"}; add_prod("DeclTail", r, 3); }
    { const char *r[] = {";"}; add_prod("DeclTail", r, 1); }

    /* Type → int | float | string */
    { const char *r[] = {"int"};    add_prod("Type", r, 1); }
    { const char *r[] = {"float"};  add_prod("Type", r, 1); }
    { const char *r[] = {"string"}; add_prod("Type", r, 1); }

    /* Assignment → IDENTIFIER = Expr ; */
    { const char *r[] = {"IDENTIFIER","=","Expr",";"}; add_prod("Assignment", r, 4); }

    /* IfStmt → if ( Condition ) Block ElsePart */
    { const char *r[] = {"if","(","Condition",")","Block","ElsePart"}; add_prod("IfStmt", r, 6); }

    /* ElsePart → else Block | EPS */
    { const char *r[] = {"else","Block"}; add_prod("ElsePart", r, 2); }
    { const char *r[] = {EPS}; add_prod("ElsePart", r, 1); }

    /* WhileStmt → while ( Condition ) Block */
    { const char *r[] = {"while","(","Condition",")","Block"}; add_prod("WhileStmt", r, 5); }

    /* ForStmt → for ( Assignment Condition ; ForAssign ) Block */
    { const char *r[] = {"for","(","Assignment","Condition",";","ForAssign",")","Block"}; add_prod("ForStmt", r, 8); }

    /* ReturnStmt → return Expr ; */
    { const char *r[] = {"return","Expr",";"}; add_prod("ReturnStmt", r, 3); }

    /* PrintStmt → print ( Expr ) ; */
    { const char *r[] = {"print","(","Expr",")",";"}; add_prod("PrintStmt", r, 5); }

    /* FuncDecl → func IDENTIFIER ( ParamList ) Block */
    { const char *r[] = {"func","IDENTIFIER","(","ParamList",")","Block"}; add_prod("FuncDecl", r, 6); }

    /* ParamList → Type IDENTIFIER MoreParams | EPS */
    { const char *r[] = {"Type","IDENTIFIER","MoreParams"}; add_prod("ParamList", r, 3); }
    { const char *r[] = {EPS}; add_prod("ParamList", r, 1); }

    /* MoreParams → , Type IDENTIFIER MoreParams | EPS */
    { const char *r[] = {",","Type","IDENTIFIER","MoreParams"}; add_prod("MoreParams", r, 4); }
    { const char *r[] = {EPS}; add_prod("MoreParams", r, 1); }

    /* Block → { StatementList } */
    { const char *r[] = {"{","StatementList","}"}; add_prod("Block", r, 3); }

    /* Condition → Expr RelOp Expr */
    { const char *r[] = {"Expr","RelOp","Expr"}; add_prod("Condition", r, 3); }

    /* RelOp → == | != | < | > | <= | >= */
    { const char *r[] = {"=="}; add_prod("RelOp", r, 1); }
    { const char *r[] = {"!="}; add_prod("RelOp", r, 1); }
    { const char *r[] = {"<"};  add_prod("RelOp", r, 1); }
    { const char *r[] = {">"};  add_prod("RelOp", r, 1); }
    { const char *r[] = {"<="}; add_prod("RelOp", r, 1); }
    { const char *r[] = {">="}; add_prod("RelOp", r, 1); }

    /* Expr → Term ExprRest */
    { const char *r[] = {"Term","ExprRest"}; add_prod("Expr", r, 2); }

    /* ExprRest → + Term ExprRest | - Term ExprRest | EPS */
    { const char *r[] = {"+","Term","ExprRest"}; add_prod("ExprRest", r, 3); }
    { const char *r[] = {"-","Term","ExprRest"}; add_prod("ExprRest", r, 3); }
    { const char *r[] = {EPS}; add_prod("ExprRest", r, 1); }

    /* Term → Factor TermRest */
    { const char *r[] = {"Factor","TermRest"}; add_prod("Term", r, 2); }

    /* TermRest → * Factor TermRest | / Factor TermRest | EPS */
    { const char *r[] = {"*","Factor","TermRest"}; add_prod("TermRest", r, 3); }
    { const char *r[] = {"/","Factor","TermRest"}; add_prod("TermRest", r, 3); }
    { const char *r[] = {EPS}; add_prod("TermRest", r, 1); }

    /* Factor → ( Expr ) | IDENTIFIER FactorRest | INTEGER | FLOAT | STRING */
    { const char *r[] = {"(","Expr",")"}; add_prod("Factor", r, 3); }
    { const char *r[] = {"IDENTIFIER","FactorRest"}; add_prod("Factor", r, 2); }
    { const char *r[] = {"INTEGER"}; add_prod("Factor", r, 1); }
    { const char *r[] = {"FLOAT"};   add_prod("Factor", r, 1); }
    { const char *r[] = {"STRING"};  add_prod("Factor", r, 1); }

    /* FactorRest → ( ArgList ) | EPS */
    { const char *r[] = {"(","ArgList",")"}; add_prod("FactorRest", r, 3); }
    { const char *r[] = {EPS}; add_prod("FactorRest", r, 1); }

    /* ArgList → Expr MoreArgs | EPS */
    { const char *r[] = {"Expr","MoreArgs"}; add_prod("ArgList", r, 2); }
    { const char *r[] = {EPS}; add_prod("ArgList", r, 1); }

    /* MoreArgs → , Expr MoreArgs | EPS */
    { const char *r[] = {",","Expr","MoreArgs"}; add_prod("MoreArgs", r, 3); }
    { const char *r[] = {EPS}; add_prod("MoreArgs", r, 1); }

    /* ForAssign → IDENTIFIER = Expr (no semicolon) */
    { const char *r[] = {"IDENTIFIER","=","Expr"}; add_prod("ForAssign", r, 3); }
}

/* ── FIRST / FOLLOW sets ──────────────────────────────────── */
#define MAX_SET_SIZE 64
typedef struct { char syms[MAX_SET_SIZE][MAX_SYM_LEN]; int count; } SymSet;

static SymSet first_sets[64];  /* indexed by nt_count order */
static SymSet follow_sets[64];

static int set_contains(const SymSet *s, const char *sym) {
    for (int i = 0; i < s->count; i++)
        if (strcmp(s->syms[i], sym) == 0) return 1;
    return 0;
}

static int set_add(SymSet *s, const char *sym) {
    if (set_contains(s, sym)) return 0;
    if (s->count >= MAX_SET_SIZE) return 0;
    strncpy(s->syms[s->count++], sym, MAX_SYM_LEN - 1);
    s->syms[s->count - 1][MAX_SYM_LEN - 1] = '\0';
    return 1;
}

static int nt_index(const char *nt) {
    for (int i = 0; i < nt_count; i++)
        if (strcmp(non_terminals[i], nt) == 0) return i;
    return -1;
}

/* Compute FIRST of a sequence; returns 1 if epsilon in result */
static int first_of_seq(const char **seq, int len, SymSet *result) {
    int all_eps = 1;
    for (int i = 0; i < len; i++) {
        const char *sym = seq[i];
        if (strcmp(sym, EPS) == 0) { continue; }
        if (is_nt(sym)) {
            int idx = nt_index(sym);
            if (idx < 0) { all_eps = 0; break; }
            /* Add FIRST(sym) - {EPS} to result */
            int has_eps = 0;
            for (int k = 0; k < first_sets[idx].count; k++) {
                if (strcmp(first_sets[idx].syms[k], EPS) == 0) { has_eps = 1; continue; }
                set_add(result, first_sets[idx].syms[k]);
            }
            if (!has_eps) { all_eps = 0; break; }
        } else {
            set_add(result, sym);
            all_eps = 0;
            break;
        }
    }
    if (all_eps) set_add(result, EPS);
    return all_eps;
}

static void compute_first(void) {
    memset(first_sets, 0, sizeof(first_sets));
    int changed = 1;
    while (changed) {
        changed = 0;
        for (int pi = 0; pi < prod_count; pi++) {
            Production *p = &productions[pi];
            int idx = nt_index(p->lhs);
            if (idx < 0) continue;
            SymSet tmp; memset(&tmp, 0, sizeof(tmp));
            const char *seq[MAX_SYMBOLS];
            for (int k = 0; k < p->rhs_len; k++) seq[k] = p->rhs[k];
            int has_eps = first_of_seq(seq, p->rhs_len, &tmp);
            (void)has_eps;
            for (int k = 0; k < tmp.count; k++)
                if (set_add(&first_sets[idx], tmp.syms[k])) changed = 1;
        }
    }
}

static void compute_follow(void) {
    memset(follow_sets, 0, sizeof(follow_sets));
    /* Program → $ */
    int pidx = nt_index("Program");
    if (pidx >= 0) set_add(&follow_sets[pidx], "$");

    int changed = 1;
    while (changed) {
        changed = 0;
        for (int pi = 0; pi < prod_count; pi++) {
            Production *p = &productions[pi];
            int lhs_idx = nt_index(p->lhs);
            if (lhs_idx < 0) continue;
            for (int pos = 0; pos < p->rhs_len; pos++) {
                const char *sym = p->rhs[pos];
                if (!is_nt(sym)) continue;
                int sym_idx = nt_index(sym);
                if (sym_idx < 0) continue;
                /* FIRST of the rest of the production */
                SymSet tmp; memset(&tmp, 0, sizeof(tmp));
                const char *rest[MAX_SYMBOLS];
                int rlen = p->rhs_len - pos - 1;
                for (int k = 0; k < rlen; k++) rest[k] = p->rhs[pos + 1 + k];
                int all_eps = first_of_seq(rest, rlen, &tmp);
                /* Add FIRST(rest) - {EPS} to FOLLOW(sym) */
                for (int k = 0; k < tmp.count; k++) {
                    if (strcmp(tmp.syms[k], EPS) == 0) continue;
                    if (set_add(&follow_sets[sym_idx], tmp.syms[k])) changed = 1;
                }
                /* If EPS in FIRST(rest), add FOLLOW(lhs) */
                if (all_eps) {
                    for (int k = 0; k < follow_sets[lhs_idx].count; k++)
                        if (set_add(&follow_sets[sym_idx], follow_sets[lhs_idx].syms[k])) changed = 1;
                }
            }
        }
    }
}

/* ── Build parse table ────────────────────────────────────── */
static char pt_nt[MAX_PT_ENTRIES][MAX_SYM_LEN];
static char pt_term[MAX_PT_ENTRIES][MAX_SYM_LEN];
static char pt_prod[MAX_PT_ENTRIES][MAX_PROD_LEN];
static int  pt_size = 0;

static void pt_add(const char *nt, const char *term, const char *prod) {
    /* Check for existing entry (first one wins, no overwrite) */
    for (int i = 0; i < pt_size; i++)
        if (strcmp(pt_nt[i], nt) == 0 && strcmp(pt_term[i], term) == 0) return;
    if (pt_size >= MAX_PT_ENTRIES) return;
    strncpy(pt_nt[pt_size],   nt,   MAX_SYM_LEN  - 1);
    strncpy(pt_term[pt_size], term, MAX_SYM_LEN  - 1);
    strncpy(pt_prod[pt_size], prod, MAX_PROD_LEN - 1);
    pt_nt[pt_size][MAX_SYM_LEN - 1]   = '\0';
    pt_term[pt_size][MAX_SYM_LEN - 1] = '\0';
    pt_prod[pt_size][MAX_PROD_LEN - 1] = '\0';
    pt_size++;
}

static void build_table_entries(void) {
    for (int pi = 0; pi < prod_count; pi++) {
        Production *p = &productions[pi];
        int lhs_idx = nt_index(p->lhs);
        if (lhs_idx < 0) continue;

        /* Build production string "LHS → rhs1 rhs2 ..." */
        char prod_str[MAX_PROD_LEN];
        snprintf(prod_str, sizeof(prod_str), "%s \xe2\x86\x92", p->lhs); /* UTF-8 → */
        for (int k = 0; k < p->rhs_len; k++) {
            strncat(prod_str, " ", sizeof(prod_str) - strlen(prod_str) - 1);
            strncat(prod_str, strcmp(p->rhs[k], EPS) == 0 ? "\xce\xb5" : p->rhs[k],
                    sizeof(prod_str) - strlen(prod_str) - 1);
        }

        /* FIRST of this production's RHS */
        SymSet tmp; memset(&tmp, 0, sizeof(tmp));
        const char *seq[MAX_SYMBOLS];
        for (int k = 0; k < p->rhs_len; k++) seq[k] = p->rhs[k];
        first_of_seq(seq, p->rhs_len, &tmp);

        /* For each terminal in FIRST(rhs) − {EPS}: M[LHS, t] = prod */
        for (int k = 0; k < tmp.count; k++) {
            if (strcmp(tmp.syms[k], EPS) == 0) continue;
            pt_add(p->lhs, tmp.syms[k], prod_str);
        }
        /* If EPS in FIRST(rhs), for each t in FOLLOW(LHS): M[LHS, t] = prod */
        if (set_contains(&tmp, EPS)) {
            for (int k = 0; k < follow_sets[lhs_idx].count; k++)
                pt_add(p->lhs, follow_sets[lhs_idx].syms[k], prod_str);
        }
    }
}

/* ── Public API ───────────────────────────────────────────── */
ParseTable parse_table_build(void) {
    init_grammar();
    compute_first();
    compute_follow();
    pt_size = 0;
    build_table_entries();

    ParseTable pt;
    pt.count = pt_size;
    for (int i = 0; i < pt_size && i < MAX_PT_ENTRIES; i++) {
        strncpy(pt.entries[i].nt,         pt_nt[i],   MAX_SYM_LEN  - 1);
        strncpy(pt.entries[i].terminal,   pt_term[i], MAX_SYM_LEN  - 1);
        strncpy(pt.entries[i].production, pt_prod[i], MAX_PROD_LEN - 1);
        pt.entries[i].nt[MAX_SYM_LEN - 1]         = '\0';
        pt.entries[i].terminal[MAX_SYM_LEN - 1]   = '\0';
        pt.entries[i].production[MAX_PROD_LEN - 1] = '\0';
    }
    return pt;
}

const char *parse_table_lookup(const ParseTable *pt, const char *nt, const char *terminal) {
    for (int i = 0; i < pt->count; i++)
        if (strcmp(pt->entries[i].nt, nt) == 0 && strcmp(pt->entries[i].terminal, terminal) == 0)
            return pt->entries[i].production;
    return NULL;
}

void parse_table_to_json(const ParseTable *pt, StrBuf *out) {
    /* Build: { "NT": { "term": "prod", ... }, ... } */
    buf_append(out, "{");
    int nt_first = 1;
    for (int ni = 0; ni < nt_count; ni++) {
        const char *nt = non_terminals[ni];
        /* Collect all entries for this NT */
        int found = 0;
        for (int i = 0; i < pt->count; i++)
            if (strcmp(pt->entries[i].nt, nt) == 0) { found = 1; break; }
        if (!found) continue;

        if (!nt_first) buf_append(out, ",");
        nt_first = 0;
        buf_append_json_str(out, nt);
        buf_append(out, ":{");
        int t_first = 1;
        for (int i = 0; i < pt->count; i++) {
            if (strcmp(pt->entries[i].nt, nt) != 0) continue;
            if (!t_first) buf_append(out, ",");
            t_first = 0;
            buf_append_json_str(out, pt->entries[i].terminal);
            buf_append(out, ":");
            buf_append_json_str(out, pt->entries[i].production);
        }
        buf_append(out, "}");
    }
    buf_append(out, "}");
}
