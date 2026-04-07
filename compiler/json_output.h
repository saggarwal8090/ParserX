#ifndef JSON_OUTPUT_H
#define JSON_OUTPUT_H

#include <stddef.h>

/* ── Dynamic string buffer ─────────────────── */
typedef struct {
    char  *data;
    size_t len;
    size_t cap;
} StrBuf;

void buf_init(StrBuf *b);
void buf_free(StrBuf *b);
void buf_append(StrBuf *b, const char *s);
void buf_appendf(StrBuf *b, const char *fmt, ...);
void buf_append_json_str(StrBuf *b, const char *s); /* appends quoted+escaped JSON string */

#endif /* JSON_OUTPUT_H */
