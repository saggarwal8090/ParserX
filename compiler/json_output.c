#include "json_output.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

void buf_init(StrBuf *b) {
    b->data = (char *)malloc(256);
    if (b->data) b->data[0] = '\0';
    b->len  = 0;
    b->cap  = 256;
}

void buf_free(StrBuf *b) {
    free(b->data);
    b->data = NULL;
    b->len  = 0;
    b->cap  = 0;
}

static void buf_grow(StrBuf *b, size_t needed) {
    if (b->len + needed + 1 <= b->cap) return;
    size_t newcap = b->cap * 2 + needed + 256;
    char *nd = (char *)realloc(b->data, newcap);
    if (!nd) return; /* silently truncate on OOM */
    b->data = nd;
    b->cap  = newcap;
}

void buf_append(StrBuf *b, const char *s) {
    if (!s || !b->data) return;
    size_t slen = strlen(s);
    buf_grow(b, slen);
    if (!b->data) return;
    memcpy(b->data + b->len, s, slen + 1);
    b->len += slen;
}

void buf_appendf(StrBuf *b, const char *fmt, ...) {
    if (!b->data) return;
    char tmp[2048];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    buf_append(b, tmp);
}

void buf_append_json_str(StrBuf *b, const char *s) {
    buf_append(b, "\"");
    if (!s) { buf_append(b, "\""); return; }
    for (const char *p = s; *p; p++) {
        unsigned char c = (unsigned char)*p;
        if      (c == '"')  buf_append(b, "\\\"");
        else if (c == '\\') buf_append(b, "\\\\");
        else if (c == '\n') buf_append(b, "\\n");
        else if (c == '\r') buf_append(b, "\\r");
        else if (c == '\t') buf_append(b, "\\t");
        else if (c < 0x20) {
            char esc[8];
            snprintf(esc, sizeof(esc), "\\u%04x", c);
            buf_append(b, esc);
        } else {
            char one[2] = { (char)c, '\0' };
            buf_append(b, one);
        }
    }
    buf_append(b, "\"");
}
