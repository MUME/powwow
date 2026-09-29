/*
 *  width.c  --  display width of UTF-8 text
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 */

#include "../config.h"

#include <ctype.h>
#include <stdbool.h>
#ifdef USE_LOCALE
#  include <wchar.h>
#  include <wctype.h>
#endif
#ifdef HAVE_LIBGRAPHEME
#  include <grapheme.h>
#endif

#include "charset.h"
#include "width.h"

int cp_width(uint32_t cp)
{
    if (cp < 0x20 || cp == 0x7f)
        return 0;               /* control characters */
#ifdef USE_LOCALE
    int w = wcwidth((wchar_t)cp);
    if (w >= 0)
        return w;
    if (cp >= 0x80 && cp < 0xa0)
        return 0;               /* C1 control characters */
    return 1;                   /* shown as '?' by the tty layer */
#else
    if (cp >= 0x80 && cp < 0xa0)
        return 0;
    if (cp >= 0x300 && cp <= 0x36f)
        return 0;               /* combining diacritical marks */
    return 1;
#endif
}

#ifndef HAVE_LIBGRAPHEME
static bool is_combining(uint32_t cp)
{
    return cp >= 0x300 && cp_width(cp) == 0;
}
#endif

static bool is_control(uint32_t cp)
{
    return cp < 0x20 || cp == 0x7f;
}

static bool is_regional_indicator(uint32_t cp)
{
    return cp >= 0x1f1e6 && cp <= 0x1f1ff;
}

void cell_start(struct cell_state *c, uint32_t cp)
{
    c->first = c->prev = cp;
    c->gstate = 0;
    c->width = cp_width(cp);
}

bool cell_add(struct cell_state *c, uint32_t cp)
{
    if (is_control(c->prev) || is_control(cp))
        return false;           /* also keep CR LF apart */
#ifdef HAVE_LIBGRAPHEME
    uint_least16_t gstate = c->gstate;
    if (grapheme_is_character_break(c->prev, cp, &gstate))
        return false;
    c->gstate = gstate;
#else
    /* combining characters, and characters after a ZWJ as in emoji
     * sequences */
    if (!is_combining(cp) && c->prev != 0x200d)
        return false;
#endif
    c->prev = cp;
    /*
     * like the terminals that draw grapheme clusters as a unit: the
     * width of the first character, but emoji presentation (VS16) and
     * flags (pairs of regional indicators) take two columns
     */
    if (c->width == 1
        && (cp == 0xfe0f
            || (is_regional_indicator(c->first) && is_regional_indicator(cp))))
        c->width = 2;
    return true;
}

/* read the cell at 's' into 'c'; returns its length */
static size_t cell_read(struct cell_state *c, const char *s, size_t len)
{
    uint32_t cp;
    if (len == 0) {
        cell_start(c, 0);
        return 0;
    }
    size_t n = utf8_get(s, len, &cp);
    cell_start(c, cp);
    while (n < len) {
        int k = utf8_get(s + n, len - n, &cp);
        if (!cell_add(c, cp))
            break;
        n += k;
    }
    return n;
}

size_t utf8_cell_len(const char *s, size_t len)
{
    struct cell_state c;
    return cell_read(&c, s, len);
}

int utf8_cell_width(const char *s, size_t len)
{
    struct cell_state c;
    cell_read(&c, s, len);
    return c.width;
}

int utf8_width(const char *s, size_t len)
{
    enum { NORM, ESCAPE, BRACKET } state = NORM;
    const char *end = s + len;
    int w = 0;

    while (s < end) {
        struct cell_state c;
        size_t n = cell_read(&c, s, end - s);
        uint32_t cp = c.first;
        switch (state) {
        case NORM:
            if (cp == '\033')
                state = ESCAPE;
            else
                w += c.width;
            break;
        case ESCAPE:
            state = cp == '[' ? BRACKET : NORM;
            break;
        case BRACKET:
            if (cp < 0x80 && isalpha(cp))
                state = NORM;
            break;
        }
        s += n;
    }
    return w;
}

const char *utf8_next_cell(const char *s, const char *end)
{
    if (s >= end)
        return end;
    return s + utf8_cell_len(s, end - s);
}

const char *utf8_prev_cell(const char *start, const char *s)
{
    if (s <= start)
        return start;
    /*
     * find a cell boundary before 's' and scan forward from it: there
     * is one before a printable ASCII character that follows another
     * ASCII character (or begins the text)
     */
    const char *p = s - 1;
    while (p > start && !((unsigned char)p[-1] < 0x80
                          && (unsigned char)*p >= 0x20
                          && (unsigned char)*p < 0x80))
        --p;
    for (;;) {
        const char *q = p + utf8_cell_len(p, s - p);
        if (q >= s)
            return p;
        p = q;
    }
}
