/*
 *  charset.c  --  conversion between ISO 8859-1 and remote link charsets
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 */

#include <string.h>
#include <strings.h>

#include "charset.h"

const char *charset_name(enum charset cs)
{
    switch (cs) {
    case CS_UTF8:  return "utf-8";
    case CS_ASCII: return "us-ascii";
    default:       return "iso-8859-1";
    }
}

int charset_lookup(const char *name, size_t len)
{
    static const struct {
        const char *name;
        enum charset cs;
    } names[] = {
        { "UTF-8",            CS_UTF8   },
        { "UTF8",             CS_UTF8   },
        { "csUTF8",           CS_UTF8   },
        { "ISO-8859-1",       CS_LATIN1 },
        { "ISO_8859-1:1987",  CS_LATIN1 },
        { "ISO_8859-1",       CS_LATIN1 },
        { "ISO8859-1",        CS_LATIN1 },
        { "latin1",           CS_LATIN1 },
        { "l1",               CS_LATIN1 },
        { "IBM819",           CS_LATIN1 },
        { "CP819",            CS_LATIN1 },
        { "csISOLatin1",      CS_LATIN1 },
        { "US-ASCII",         CS_ASCII  },
        { "ASCII",            CS_ASCII  },
        { "ANSI_X3.4-1968",   CS_ASCII  },
        { "ANSI_X3.4-1986",   CS_ASCII  },
        { "ISO_646.irv:1991", CS_ASCII  },
        { "ISO646-US",        CS_ASCII  },
        { "us",               CS_ASCII  },
        { "IBM367",           CS_ASCII  },
        { "cp367",            CS_ASCII  },
        { "csASCII",          CS_ASCII  },
    };

    for (size_t i = 0; i < sizeof names / sizeof names[0]; ++i)
        if (strlen(names[i].name) == len
            && strncasecmp(names[i].name, name, len) == 0)
            return names[i].cs;
    return -1;
}

void utf8_decoder_reset(struct utf8_decoder *d)
{
    d->need = 0;
}

/*
 * Invalid input results in one replacement character per maximal
 * subpart, as recommended by Unicode.  The lo/hi continuation byte
 * range rejects overlong forms, surrogates and values above U+10FFFF.
 */
int utf8_decode_byte(struct utf8_decoder *d, unsigned char b, char *out)
{
    int n = 0;

    if (d->need) {
        if (b >= d->lo && b <= d->hi) {
            d->cp = (d->cp << 6) | (b & 0x3f);
            d->lo = 0x80;
            d->hi = 0xbf;
            if (--d->need == 0)
                out[n++] = d->cp <= 0xff ? (char)d->cp : CHARSET_REPLACEMENT;
            return n;
        }
        /* truncated sequence; reprocess 'b' as a new character */
        d->need = 0;
        out[n++] = CHARSET_REPLACEMENT;
    }

    d->lo = 0x80;
    d->hi = 0xbf;
    if (b < 0x80) {
        out[n++] = b;
    } else if (b >= 0xc2 && b <= 0xdf) {
        d->need = 1;
        d->cp = b & 0x1f;
    } else if (b >= 0xe0 && b <= 0xef) {
        d->need = 2;
        d->cp = b & 0x0f;
        if (b == 0xe0)
            d->lo = 0xa0;       /* overlong */
        else if (b == 0xed)
            d->hi = 0x9f;       /* surrogates */
    } else if (b >= 0xf0 && b <= 0xf4) {
        d->need = 3;
        d->cp = b & 0x07;
        if (b == 0xf0)
            d->lo = 0x90;       /* overlong */
        else if (b == 0xf4)
            d->hi = 0x8f;       /* > U+10FFFF */
    } else {
        out[n++] = CHARSET_REPLACEMENT;
    }
    return n;
}

int utf8_decoder_flush(struct utf8_decoder *d, char *out)
{
    if (!d->need)
        return 0;
    d->need = 0;
    *out = CHARSET_REPLACEMENT;
    return 1;
}

size_t utf8_to_latin1(char *buf, size_t len)
{
    struct utf8_decoder d = { .need = 0 };
    char *to = buf;
    /* output never overtakes input for a fresh decoder */
    for (size_t i = 0; i < len; ++i)
        to += utf8_decode_byte(&d, buf[i], to);
    to += utf8_decoder_flush(&d, to);
    return to - buf;
}

size_t latin1_encode(enum charset cs, const char *src, size_t len,
                      char *dst)
{
    char *to = dst;
    for (size_t i = 0; i < len; ++i) {
        unsigned char c = src[i];
        if (c < 0x80 || cs == CS_LATIN1) {
            *to++ = c;
        } else if (cs == CS_ASCII) {
            *to++ = CHARSET_REPLACEMENT;
        } else {
            *to++ = 0xc0 | (c >> 6);
            *to++ = 0x80 | (c & 0x3f);
        }
    }
    return to - dst;
}

/*
 * Conversion between remote link charsets and UTF-8
 */

/* decode UTF-8, validating it; same rules as utf8_decode_byte() */
static int utf8_check_byte(struct utf8_decoder *d, unsigned char b,
                           char *out)
{
    int n = 0;

    if (d->need) {
        if (b >= d->lo && b <= d->hi) {
            d->cp = (d->cp << 6) | (b & 0x3f);
            d->lo = 0x80;
            d->hi = 0xbf;
            if (--d->need == 0)
                n += utf8_put(d->cp, out);
            return n;
        }
        /* truncated sequence; reprocess 'b' as a new character */
        d->need = 0;
        memcpy(out, UTF8_REPLACEMENT, UTF8_REPLACEMENT_LEN);
        n += UTF8_REPLACEMENT_LEN;
    }

    d->lo = 0x80;
    d->hi = 0xbf;
    if (b < 0x80) {
        out[n++] = b;
    } else if (b >= 0xc2 && b <= 0xdf) {
        d->need = 1;
        d->cp = b & 0x1f;
    } else if (b >= 0xe0 && b <= 0xef) {
        d->need = 2;
        d->cp = b & 0x0f;
        if (b == 0xe0)
            d->lo = 0xa0;       /* overlong */
        else if (b == 0xed)
            d->hi = 0x9f;       /* surrogates */
    } else if (b >= 0xf0 && b <= 0xf4) {
        d->need = 3;
        d->cp = b & 0x07;
        if (b == 0xf0)
            d->lo = 0x90;       /* overlong */
        else if (b == 0xf4)
            d->hi = 0x8f;       /* > U+10FFFF */
    } else {
        memcpy(out + n, UTF8_REPLACEMENT, UTF8_REPLACEMENT_LEN);
        n += UTF8_REPLACEMENT_LEN;
    }
    return n;
}

int charset_decode_byte(enum charset cs, struct utf8_decoder *d,
                        unsigned char b, char *out)
{
    switch (cs) {
    case CS_UTF8:
        return utf8_check_byte(d, b, out);
    case CS_ASCII:
        if (b < 0x80)
            break;
        memcpy(out, UTF8_REPLACEMENT, UTF8_REPLACEMENT_LEN);
        return UTF8_REPLACEMENT_LEN;
    default:
        return utf8_put(b, out);
    }
    *out = b;
    return 1;
}

int charset_decode_flush(struct utf8_decoder *d, char *out)
{
    if (!d->need)
        return 0;
    d->need = 0;
    memcpy(out, UTF8_REPLACEMENT, UTF8_REPLACEMENT_LEN);
    return UTF8_REPLACEMENT_LEN;
}

size_t utf8_sanitize(const char *src, size_t len, char *dst)
{
    struct utf8_decoder d = { .need = 0 };
    char *to = dst;
    for (size_t i = 0; i < len; ++i)
        to += utf8_check_byte(&d, src[i], to);
    to += charset_decode_flush(&d, to);
    return to - dst;
}

size_t latin1_to_utf8(const char *src, size_t len, char *dst)
{
    char *to = dst;
    for (size_t i = 0; i < len; ++i)
        to += utf8_put((unsigned char)src[i], to);
    return to - dst;
}

size_t charset_encode(enum charset cs, const char *src, size_t len,
                      char *dst)
{
    if (cs == CS_UTF8) {
        memmove(dst, src, len);
        return len;
    }

    const uint32_t max = cs == CS_ASCII ? 0x7f : 0xff;
    char *to = dst;
    while (len > 0) {
        uint32_t cp;
        int n = utf8_get(src, len, &cp);
        *to++ = cp <= max ? (char)cp : CHARSET_REPLACEMENT;
        src += n;
        len -= n;
    }
    return to - dst;
}

/*
 * UTF-8 helpers
 */

int utf8_get(const char *s, size_t len, uint32_t *cp)
{
    struct utf8_decoder d = { .need = 0 };
    char out[CHARSET_DECODE_MAX];

    if (len == 0) {
        *cp = UNICODE_REPLACEMENT;
        return 0;
    }
    for (size_t i = 0; i < len && i < UTF8_MAX; ++i) {
        int n = utf8_check_byte(&d, s[i], out);
        if (d.need)
            continue;           /* incomplete */
        if (i == 0 && n == 1) {
            *cp = (unsigned char)s[0];
            return 1;
        }
        if (n > 0 && memcmp(out, s, i + 1) == 0) {
            *cp = d.cp;         /* complete multibyte character */
            return i + 1;
        }
        break;
    }
    *cp = UNICODE_REPLACEMENT;
    return 1;
}

int utf8_put(uint32_t cp, char *dst)
{
    unsigned char *d = (unsigned char *)dst;
    if (cp < 0x80) {
        d[0] = cp;
        return 1;
    }
    if (cp < 0x800) {
        d[0] = 0xc0 | (cp >> 6);
        d[1] = 0x80 | (cp & 0x3f);
        return 2;
    }
    if ((cp >= 0xd800 && cp <= 0xdfff) || cp > 0x10ffff)
        cp = UNICODE_REPLACEMENT;
    if (cp < 0x10000) {
        d[0] = 0xe0 | (cp >> 12);
        d[1] = 0x80 | ((cp >> 6) & 0x3f);
        d[2] = 0x80 | (cp & 0x3f);
        return 3;
    }
    d[0] = 0xf0 | (cp >> 18);
    d[1] = 0x80 | ((cp >> 12) & 0x3f);
    d[2] = 0x80 | ((cp >> 6) & 0x3f);
    d[3] = 0x80 | (cp & 0x3f);
    return 4;
}

const char *utf8_prev(const char *start, const char *s)
{
    if (s <= start)
        return start;
    const char *p = s - 1;
    /* back over at most three continuation bytes */
    for (int i = 0; i < UTF8_MAX - 1 && p > start
             && ((unsigned char)*p & 0xc0) == 0x80; ++i)
        --p;
    uint32_t cp;
    /* if that is not the start of a character ending at 's', step
     * back a single (invalid) byte */
    if (p + utf8_get(p, s - p, &cp) != s)
        p = s - 1;
    return p;
}

size_t utf8_count(const char *s, size_t len)
{
    size_t n = 0;
    uint32_t cp;
    for (size_t i = 0; i < len; ++n)
        i += utf8_get(s + i, len - i, &cp);
    return n;
}

size_t utf8_offset(const char *s, size_t len, size_t n)
{
    size_t i = 0;
    uint32_t cp;
    while (n-- > 0 && i < len)
        i += utf8_get(s + i, len - i, &cp);
    return i;
}
