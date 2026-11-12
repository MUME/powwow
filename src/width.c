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
#include "defines.h"
#include "width.h"

/* the characters that join or widen a grapheme cluster */
enum {
    UNICODE_COMBINING_FIRST = 0x300,    /* COMBINING DIACRITICAL MARKS */
    UNICODE_COMBINING_LAST = 0x36f,
    UNICODE_ZWJ = 0x200d,               /* ZERO WIDTH JOINER */
    UNICODE_VS16 = 0xfe0f,              /* VARIATION SELECTOR-16 */
    UNICODE_REGIONAL_A = 0x1f1e6,       /* REGIONAL INDICATOR SYMBOL */
    UNICODE_REGIONAL_Z = 0x1f1ff        /* LETTER A to Z, of flags */
};

int cp_width(uint32_t cp)
{
    if (cp < ' ' || cp == ASCII_DEL)
        return 0;               /* control characters */
#ifdef USE_LOCALE
#if WCHAR_MAX < UNICODE_MAX
    /* a wchar_t of 16 bits, as on Cygwin, cannot hold the characters
     * above U+FFFF, such as most emoji: wcswidth() takes them as their
     * UTF-16 surrogates */
    if (cp > WCHAR_MAX) {
        const wchar_t utf16[] = {
            (wchar_t)utf16_high(cp), (wchar_t)utf16_low(cp)
        };
        int w = wcswidth(utf16, 2);
        return w >= 0 ? w : 1;
    }
#endif
    int w = wcwidth((wchar_t)cp);
    if (w >= 0)
        return w;
    if (unicode_is_c1(cp))
        return 0;               /* C1 control characters */
    return 1;                   /* shown as '?' by the tty layer */
#else
    if (unicode_is_c1(cp))
        return 0;
    if (cp >= UNICODE_COMBINING_FIRST && cp <= UNICODE_COMBINING_LAST)
        return 0;               /* combining diacritical marks */
    return 1;
#endif
}

/* a combining character, or one not shown, as a zero width space */
static bool is_zero_width(uint32_t cp)
{
    return cp >= UNICODE_COMBINING_FIRST && cp_width(cp) == 0;
}

static bool is_control(uint32_t cp)
{
    return cp < ' ' || cp == ASCII_DEL;
}

static bool is_regional_indicator(uint32_t cp)
{
    return cp >= UNICODE_REGIONAL_A && cp <= UNICODE_REGIONAL_Z;
}

void cell_start(struct cell_state *c, uint32_t cp)
{
    *c = (struct cell_state){
        .first = cp, .prev = cp, .gstate = 0, .width = cp_width(cp)
    };
}

bool cell_add(struct cell_state *c, uint32_t cp)
{
    if (is_control(c->prev) || is_control(cp))
        return false;           /* also keep CR LF apart */
    /*
     * as terminals draw them, a character not shown, such as a zero
     * width space, joins the cell before it, or the one after it at the
     * start of a line, and is not a cell that the cursor stops at
     */
    bool join = (is_zero_width(cp)
                 || (c->width == 0 && is_zero_width(c->first)));
#ifdef HAVE_LIBGRAPHEME
    uint_least16_t gstate = c->gstate;
    if (grapheme_is_character_break(c->prev, cp, &gstate) && !join)
        return false;
    c->gstate = gstate;
#else
    /* combining characters, and characters after a ZWJ as in emoji
     * sequences */
    if (!join && c->prev != UNICODE_ZWJ)
        return false;
#endif
    c->prev = cp;
    if (c->width == 0)
        c->width = cp_width(cp);
    /*
     * like the terminals that draw grapheme clusters as a unit: the
     * width of the first character, but emoji presentation (VS16) and
     * flags (pairs of regional indicators) take two columns
     */
    if (c->width == 1
        && (cp == UNICODE_VS16
            || (is_regional_indicator(c->first) && is_regional_indicator(cp))))
        c->width = 2;
    return true;
}

/* read the cell at 's' into 'c'; returns its length */
static size_t cell_read(struct cell_state *c, const char *s, size_t len)
{
    if (len == 0) {
        cell_start(c, 0);
        return 0;
    }
    uint32_t cp;
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
            if (cp == ASCII_ESC)
                state = ESCAPE;
            else
                w += c.width;
            break;
        case ESCAPE:
            state = cp == '[' ? BRACKET : NORM;
            break;
        case BRACKET:
            if (cp <= ASCII_MAX && isalpha(cp))
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
    while (p > start
           && !((unsigned char)p[-1] <= ASCII_MAX
                && (unsigned char)*p >= ' '
                && (unsigned char)*p <= ASCII_MAX))
        --p;
    for (;;) {
        const char *q = p + utf8_cell_len(p, s - p);
        if (q >= s)
            return p;
        p = q;
    }
}
