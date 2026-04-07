#ifndef LEXER_H
#define LEXER_H

#define MAX_TOKENS 4096
#define MAX_TOKEN_LEN 512

typedef struct {
    char type[32];
    char value[MAX_TOKEN_LEN];
    int  line;
} Token;

typedef struct {
    Token *tokens;
    int    count;
    char   error[512];
    int    has_error;
} LexResult;

/* Tokenize source code. Returns LexResult; caller must free result.tokens. */
LexResult lexer_tokenize(const char *src, int src_len);

#endif /* LEXER_H */
