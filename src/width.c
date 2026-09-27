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
#ifdef USE_LOCALE
#  include <wchar.h>
#  include <wctype.h>
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

int utf8_width(const char *s, size_t len)
{
    enum { NORM, ESCAPE, BRACKET } state = NORM;
    const char *end = s + len;
    int w = 0;

    while (s < end) {
        uint32_t cp;
        int n = utf8_get(s, end - s, &cp);
        switch (state) {
        case NORM:
            if (cp == '\033')
                state = ESCAPE;
            else
                w += cp_width(cp);
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

static int is_combining(uint32_t cp)
{
    return cp >= 0x300 && cp_width(cp) == 0;
}

const char *utf8_next_cell(const char *s, const char *end)
{
    uint32_t cp;
    if (s >= end)
        return end;
    s += utf8_get(s, end - s, &cp);
    while (s < end) {
        int n = utf8_get(s, end - s, &cp);
        if (!is_combining(cp))
            break;
        s += n;
    }
    return s;
}

const char *utf8_prev_cell(const char *start, const char *s)
{
    while (s > start) {
        const char *p = utf8_prev(start, s);
        uint32_t cp;
        utf8_get(p, s - p, &cp);
        s = p;
        if (!is_combining(cp))
            break;
    }
    return s;
}
