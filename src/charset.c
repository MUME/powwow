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

size_t charset_encode(enum charset cs, const char *src, size_t len,
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
