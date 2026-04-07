#ifndef PARSER_H
#define PARSER_H

#include "lexer.h"
#include "parse_table.h"

typedef struct {
    int  accepted;       /* 1 = success */
    char error_msg[512]; /* set if !accepted */
} ParseResult;

/* Run LL(1) predictive parse; returns status + error message. */
ParseResult parser_run(const Token *tokens, int count, const ParseTable *pt);

#endif /* PARSER_H */
