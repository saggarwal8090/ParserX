#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lexer.h"
#include "parse_table.h"
#include "parser.h"
#include "ast.h"
#include "symbol_table.h"
#include "optimizer.h"
#include "json_output.h"

#define MAX_INPUT (1024 * 1024)  /* 1 MB */

static void print_error(const char *msg) {
    printf("{\"error\":");
    /* manual JSON string escape to avoid depending on buf */
    printf("\"");
    for (const char *p = msg; *p; p++) {
        if (*p == '"')       printf("\\\"");
        else if (*p == '\\') printf("\\\\");
        else if (*p == '\n') printf("\\n");
        else if (*p == '\r') printf("\\r");
        else                 printf("%c", *p);
    }
    printf("\",\"status\":\"error\"}\n");
}

/* ── Read entire file into a newly-malloc'd buffer ─────────── */
static char *read_file(const char *path, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);
    if (sz <= 0 || sz > MAX_INPUT) { fclose(f); return NULL; }
    char *buf = (char *)malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t n = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[n] = '\0';
    *out_len = n;
    return buf;
}

/* ── Normalize line endings: strip all \r in-place ─────────── */
static size_t strip_cr(char *buf, size_t len) {
    size_t w = 0;
    for (size_t r = 0; r < len; r++) {
        if (buf[r] != '\r') buf[w++] = buf[r];
    }
    buf[w] = '\0';
    return w;
}

int main(int argc, char *argv[]) {
    char *src = NULL;
    size_t total = 0;

    if (argc >= 2) {
        /* ── Read from file (argv[1]) ─────────────────── */
        src = read_file(argv[1], &total);
        if (!src) {
            print_error("Could not read input file.");
            return 1;
        }
    } else {
        /* ── Read from stdin ──────────────────────────── */
        src = (char *)malloc(MAX_INPUT + 1);
        if (!src) { print_error("Out of memory"); return 1; }

        int ch;
        while (total < MAX_INPUT && (ch = getchar()) != EOF) {
            src[total++] = (char)ch;
        }
        src[total] = '\0';
    }

    if (total == 0) {
        free(src);
        print_error("Empty input");
        return 1;
    }
    if (total >= MAX_INPUT) {
        free(src);
        print_error("Input too large (max 1MB)");
        return 1;
    }

    /* ── Normalize \r\n → \n (Windows pipe / file compat) ─── */
    total = strip_cr(src, total);
    if (total == 0) {
        free(src);
        print_error("Empty input after stripping CR");
        return 1;
    }

    /* ── Stage 1: Lexing ─────────────────────────── */
    LexResult lex = lexer_tokenize(src, (int)total);
    if (lex.has_error) {
        free(src);
        print_error(lex.error);
        return 1;
    }

    /* ── Stage 2: Parse table ────────────────────── */
    ParseTable pt = parse_table_build();

    /* ── Stage 3: LL(1) Parsing ──────────────────── */
    ParseResult pr = parser_run(lex.tokens, lex.count, &pt);
    if (!pr.accepted) {
        free(lex.tokens);
        free(src);
        print_error(pr.error_msg);
        return 1;
    }

    /* ── Stage 4: AST ────────────────────────────── */
    ASTResult ar = ast_build(lex.tokens, lex.count);
    if (ar.has_error) {
        free(lex.tokens);
        free(src);
        print_error(ar.error);
        return 1;
    }

    /* ── Stage 5: Symbol Table ───────────────────── */
    SymTable st = symbol_table_build(ar.root);

    /* ── Stage 6: Optimizer ──────────────────────── */
    OptResult opt = optimizer_run(ar.root, src);

    /* ── Assemble JSON output ────────────────────── */
    StrBuf out;
    buf_init(&out);

    buf_append(&out, "{");

    /* tokens */
    buf_append(&out, "\"tokens\":[");
    for (int i = 0; i < lex.count; i++) {
        if (i > 0) buf_append(&out, ",");
        buf_append(&out, "{");
        buf_append(&out, "\"type\":");  buf_append_json_str(&out, lex.tokens[i].type);
        buf_append(&out, ",\"value\":"); buf_append_json_str(&out, lex.tokens[i].value);
        buf_appendf(&out, ",\"line\":%d", lex.tokens[i].line);
        buf_append(&out, "}");
    }
    buf_append(&out, "]");

    /* parse_table */
    buf_append(&out, ",\"parse_table\":");
    parse_table_to_json(&pt, &out);

    /* ast */
    buf_append(&out, ",\"ast\":");
    ast_to_json(ar.root, &out);

    /* symbol_table */
    buf_append(&out, ",\"symbol_table\":");
    symbol_table_to_json(&st, &out);

    /* optimizer */
    buf_append(&out, ",\"optimizer\":");
    optimizer_to_json(&opt, &out);

    /* status */
    buf_append(&out, ",\"status\":\"success\",\"error\":\"\"");

    buf_append(&out, "}\n");

    /* Print */
    fputs(out.data, stdout);

    /* ── Cleanup ──────────────────────────────────── */
    buf_free(&out);
    ast_free(ar.root);
    free(lex.tokens);
    free(src);

    return 0;
}
