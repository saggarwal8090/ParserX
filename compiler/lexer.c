#include "lexer.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>

#define MAX_INPUT_SIZE (1024 * 1024)  /* 1 MB */

static const char *KEYWORDS[] = {
    "int", "float", "string", "if", "else", "while", "for",
    "return", "func", "print", NULL
};

static int is_keyword(const char *word) {
    for (int i = 0; KEYWORDS[i]; i++) {
        if (strcmp(word, KEYWORDS[i]) == 0) return 1;
    }
    return 0;
}

static Token *grow_tokens(Token *toks, int count, int *cap) {
    if (count < *cap) return toks;
    *cap = (*cap) * 2 + 64;
    Token *n = (Token *)realloc(toks, (size_t)(*cap) * sizeof(Token));
    if (!n) { free(toks); return NULL; }
    return n;
}

static void set_token(Token *t, const char *type, const char *val, int line) {
    strncpy(t->type,  type, sizeof(t->type)  - 1); t->type[sizeof(t->type)  - 1] = '\0';
    strncpy(t->value, val,  sizeof(t->value) - 1); t->value[sizeof(t->value) - 1] = '\0';
    t->line = line;
}

LexResult lexer_tokenize(const char *src, int src_len) {
    LexResult res;
    memset(&res, 0, sizeof(res));

    if (!src || src_len <= 0) {
        snprintf(res.error, sizeof(res.error), "Empty input");
        res.has_error = 1;
        return res;
    }
    if (src_len > MAX_INPUT_SIZE) {
        snprintf(res.error, sizeof(res.error), "Input too large (max 1MB)");
        res.has_error = 1;
        return res;
    }

    int cap    = 256;
    Token *toks = (Token *)malloc((size_t)cap * sizeof(Token));
    if (!toks) {
        snprintf(res.error, sizeof(res.error), "Out of memory");
        res.has_error = 1;
        return res;
    }

    int count = 0;
    int i     = 0;
    int line  = 1;

    while (i < src_len) {
        char ch = src[i];

        /* newline */
        if (ch == '\n') { line++; i++; continue; }

        /* whitespace */
        if (ch == ' ' || ch == '\t' || ch == '\r') { i++; continue; }

        /* line comment */
        if (ch == '/' && i + 1 < src_len && src[i + 1] == '/') {
            while (i < src_len && src[i] != '\n') i++;
            continue;
        }

        /* string literal */
        if (ch == '"') {
            int j = i + 1;
            while (j < src_len && src[j] != '"') {
                if (src[j] == '\n') line++;
                j++;
            }
            char val[MAX_TOKEN_LEN];
            int vlen = j - i - 1;
            if (vlen < 0) vlen = 0;
            if (vlen >= MAX_TOKEN_LEN) vlen = MAX_TOKEN_LEN - 1;
            memcpy(val, src + i + 1, (size_t)vlen);
            val[vlen] = '\0';
            toks = grow_tokens(toks, count, &cap);
            if (!toks) { res.has_error = 1; snprintf(res.error, sizeof(res.error), "OOM"); return res; }
            set_token(&toks[count++], "STRING", val, line);
            i = j + 1;
            continue;
        }

        /* number */
        if (isdigit((unsigned char)ch)) {
            int j = i;
            while (j < src_len && isdigit((unsigned char)src[j])) j++;
            int is_float = 0;
            if (j < src_len && src[j] == '.' && j + 1 < src_len && isdigit((unsigned char)src[j + 1])) {
                j++; /* skip dot */
                while (j < src_len && isdigit((unsigned char)src[j])) j++;
                is_float = 1;
            }
            char val[MAX_TOKEN_LEN];
            int vlen = j - i;
            if (vlen >= MAX_TOKEN_LEN) vlen = MAX_TOKEN_LEN - 1;
            memcpy(val, src + i, (size_t)vlen);
            val[vlen] = '\0';
            toks = grow_tokens(toks, count, &cap);
            if (!toks) { res.has_error = 1; snprintf(res.error, sizeof(res.error), "OOM"); return res; }
            set_token(&toks[count++], is_float ? "FLOAT" : "INTEGER", val, line);
            i = j;
            continue;
        }

        /* identifier / keyword */
        if (isalpha((unsigned char)ch) || ch == '_') {
            int j = i;
            while (j < src_len && (isalnum((unsigned char)src[j]) || src[j] == '_')) j++;
            char word[MAX_TOKEN_LEN];
            int wlen = j - i;
            if (wlen >= MAX_TOKEN_LEN) wlen = MAX_TOKEN_LEN - 1;
            memcpy(word, src + i, (size_t)wlen);
            word[wlen] = '\0';
            toks = grow_tokens(toks, count, &cap);
            if (!toks) { res.has_error = 1; snprintf(res.error, sizeof(res.error), "OOM"); return res; }
            set_token(&toks[count++], is_keyword(word) ? "KEYWORD" : "IDENTIFIER", word, line);
            i = j;
            continue;
        }

        /* two-char operators */
        if (i + 1 < src_len) {
            char two[3] = { src[i], src[i+1], '\0' };
            if (strcmp(two, "==") == 0 || strcmp(two, "!=") == 0 ||
                strcmp(two, "<=") == 0 || strcmp(two, ">=") == 0) {
                toks = grow_tokens(toks, count, &cap);
                if (!toks) { res.has_error = 1; snprintf(res.error, sizeof(res.error), "OOM"); return res; }
                set_token(&toks[count++], "OPERATOR", two, line);
                i += 2;
                continue;
            }
        }

        /* one-char operators */
        if (ch == '+' || ch == '-' || ch == '*' || ch == '/' ||
            ch == '=' || ch == '<' || ch == '>') {
            char val[2] = { ch, '\0' };
            toks = grow_tokens(toks, count, &cap);
            if (!toks) { res.has_error = 1; snprintf(res.error, sizeof(res.error), "OOM"); return res; }
            set_token(&toks[count++], "OPERATOR", val, line);
            i++;
            continue;
        }

        /* delimiters */
        if (ch == '(' || ch == ')' || ch == '{' || ch == '}' ||
            ch == ';' || ch == ',') {
            char val[2] = { ch, '\0' };
            toks = grow_tokens(toks, count, &cap);
            if (!toks) { res.has_error = 1; snprintf(res.error, sizeof(res.error), "OOM"); return res; }
            set_token(&toks[count++], "DELIMITER", val, line);
            i++;
            continue;
        }

        /* unknown — skip */
        i++;
    }

    /* EOF token */
    toks = grow_tokens(toks, count, &cap);
    if (!toks) { res.has_error = 1; snprintf(res.error, sizeof(res.error), "OOM"); return res; }
    set_token(&toks[count++], "EOF", "", line);

    res.tokens = toks;
    res.count  = count;
    return res;
}
