/*
 *  test_charset.c  --  unit tests for charset.c
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 */

#include <stdio.h>
#include <string.h>

#include "charset.h"

static int failures;

/* decode 'in' feeding the bytes in chunks of 'chunk' with a flush at
 * the end, like tcp_read() does at a line break */
static void check_decode(const char *what, const char *in, size_t inlen,
                         const char *expect, size_t chunk)
{
    struct utf8_decoder d;
    char out[256];
    size_t n = 0;

    utf8_decoder_reset(&d);
    for (size_t i = 0; i < inlen; i += chunk)
        for (size_t j = i; j < i + chunk && j < inlen; ++j)
            n += utf8_decode_byte(&d, in[j], out + n);
    n += utf8_decoder_flush(&d, out + n);

    size_t elen = strlen(expect);
    if (n != elen || memcmp(out, expect, n) != 0) {
        printf("FAIL decode %s (chunk %zu): got \"%.*s\", expected \"%s\"\n",
               what, chunk, (int)n, out, expect);
        ++failures;
    }
}

#define DECODE(what, in, expect) do {                                 \
        for (size_t c = 1; c <= 4; ++c)                               \
            check_decode(what, in, sizeof in - 1, expect, c);         \
    } while (0)

static void check_encode(const char *what, enum charset cs,
                         const char *in, const char *expect)
{
    char out[256];
    size_t n = charset_encode(cs, in, strlen(in), out);
    if (n != strlen(expect) || memcmp(out, expect, n) != 0) {
        printf("FAIL encode %s: got \"%.*s\"\n", what, (int)n, out);
        ++failures;
    }
}

int main(void)
{
    DECODE("ascii", "hello", "hello");
    DECODE("2-byte latin1", "caf\xc3\xa9 \xc3\xbf", "caf\xe9 \xff");
    DECODE("U+0080", "\xc2\x80", "\x80");
    DECODE("2-byte > U+00FF", "a\xc4\x80z", "a?z");
    DECODE("3-byte", "\xe2\x82\xac!", "?!");
    DECODE("4-byte", "\xf0\x9f\x98\x80!", "?!");
    DECODE("stray continuation", "a\x80\xbfz", "a??z");
    DECODE("invalid leads", "\xc0\xc1\xf5\xff", "????");
    DECODE("overlong 2-byte", "\xc0\xaf", "??");
    DECODE("overlong 3-byte", "\xe0\x80\xaf", "???");
    DECODE("overlong 4-byte", "\xf0\x80\x80\xaf", "????");
    DECODE("surrogate", "\xed\xa0\x80", "???");
    DECODE("above U+10FFFF", "\xf4\x90\x80\x80", "????");
    DECODE("max code point", "\xf4\x8f\xbf\xbf", "?");
    DECODE("truncated + ascii", "\xe2\x82z", "?z");
    DECODE("truncated + lead", "\xe2\xc3\xa9", "?\xe9");
    DECODE("truncated at end", "ab\xf0\x9f\x98", "ab?");

    /* in-place conversion of a complete buffer */
    char buf[] = "\xe2x\xc3\xa9\xe2\x82\xac";
    size_t n = utf8_to_latin1(buf, sizeof buf - 1);
    if (n != 4 || memcmp(buf, "?x\xe9?", 4) != 0) {
        printf("FAIL utf8_to_latin1: got \"%.*s\"\n", (int)n, buf);
        ++failures;
    }

    check_encode("latin1", CS_LATIN1, "caf\xe9\xff", "caf\xe9\xff");
    check_encode("utf8", CS_UTF8, "caf\xe9 \x80\xff",
                 "caf\xc3\xa9 \xc2\x80\xc3\xbf");
    check_encode("ascii", CS_ASCII, "caf\xe9\xff", "caf??");

    /* round trip of every ISO 8859-1 character */
    char all[256], enc[512], dec[512];
    for (int i = 0; i < 255; ++i)
        all[i] = i + 1;
    size_t elen = charset_encode(CS_UTF8, all, 255, enc);
    memcpy(dec, enc, elen);
    if (utf8_to_latin1(dec, elen) != 255 || memcmp(dec, all, 255) != 0) {
        printf("FAIL round trip\n");
        ++failures;
    }

    static const struct {
        const char *name;
        int cs;
    } names[] = {
        { "UTF-8", CS_UTF8 }, { "utf-8", CS_UTF8 },
        { "iso-8859-1", CS_LATIN1 }, { "ISO_8859-1:1987", CS_LATIN1 },
        { "Latin1", CS_LATIN1 }, { "US-ASCII", CS_ASCII },
        { "ansi_x3.4-1968", CS_ASCII }, { "UTF-16", -1 }, { "UTF-8x", -1 },
        { "", -1 },
    };
    for (size_t i = 0; i < sizeof names / sizeof names[0]; ++i) {
        int cs = charset_lookup(names[i].name, strlen(names[i].name));
        if (cs != names[i].cs) {
            printf("FAIL lookup \"%s\": got %d\n", names[i].name, cs);
            ++failures;
        }
    }

    if (failures == 0)
        printf("all charset tests passed\n");
    return failures != 0;
}
