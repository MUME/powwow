/*
 *  sgr.c  --  track the graphic rendition set by escape sequences
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 */

#include <stdbool.h>
#include <string.h>

#include "sgr.h"

void sgr_reset(struct sgr_state *st)
{
    st->len = 0;
    st->seq[0] = '\0';
    st->reset = 0;
}

/* a reset sequence was seen */
static void sgr_clear(struct sgr_state *st)
{
    st->len = 0;
    st->seq[0] = '\0';
    st->reset = 1;
}

/* append "ESC [ params m", dropping the oldest sequences if needed */
static void sgr_append(struct sgr_state *st, const char *params, size_t plen)
{
    size_t n = plen + 3;
    if (n >= sizeof st->seq)
        return;                 /* absurdly long; ignore it */
    while (st->len + n >= sizeof st->seq) {
        char *next = strstr(st->seq + 1, "\033[");
        size_t drop = next ? (size_t)(next - st->seq) : st->len;
        memmove(st->seq, st->seq + drop, st->len - drop + 1);
        st->len -= drop;
    }
    char *p = st->seq + st->len;
    *p++ = '\033';
    *p++ = '[';
    memcpy(p, params, plen);
    p += plen;
    *p++ = 'm';
    *p = '\0';
    st->len += n;
}

/* is the parameter [s, e) zero or empty, i.e. a reset? */
static bool is_reset(const char *s, const char *e)
{
    for (; s < e; ++s)
        if (*s != '0')
            return false;
    return true;
}

/* handle the parameters of one SGR sequence */
static void sgr_params(struct sgr_state *st, const char *params, size_t plen)
{
    const char *end = params + plen;
    const char *after_reset = NULL;     /* parameters after the last reset */
    int skip = 0;

    for (const char *s = params; s <= end; ) {
        const char *e = memchr(s, ';', end - s);
        if (e == NULL)
            e = end;
        if (skip > 0) {
            /* arguments of an extended color */
            --skip;
        } else if (is_reset(s, e)) {
            after_reset = e < end ? e + 1 : end;
        } else if ((e - s == 2) && (s[0] == '3' || s[0] == '4' || s[0] == '5')
                   && s[1] == '8' && e < end) {
            /* 38;5;n or 38;2;r;g;b (and 48, 58) */
            const char *m = e + 1;
            if (m < end && *m == '5')
                skip = 2;
            else if (m < end && *m == '2')
                skip = 4;
        }
        s = e + 1;
    }

    if (after_reset) {
        sgr_clear(st);
        if (after_reset < end)
            sgr_append(st, after_reset, end - after_reset);
    } else {
        sgr_append(st, params, plen);
    }
}

void sgr_apply(struct sgr_state *st, const char *s, size_t len)
{
    const char *end = s + len;

    while (s < end) {
        const char *esc = memchr(s, '\033', end - s);
        if (esc == NULL || end - esc < 2)
            return;
        s = esc + 1;
        if (*s == 'c') {
            sgr_clear(st);      /* full reset */
            continue;
        }
        if (*s != '[')
            continue;
        /* CSI: parameter bytes, then a final byte */
        const char *params = ++s;
        while (s < end && ((*s >= '0' && *s <= '9') || *s == ';' || *s == ':'))
            ++s;
        if (s >= end)
            return;             /* incomplete */
        if (*s == 'm')
            sgr_params(st, params, s - params);
        ++s;
    }
}
