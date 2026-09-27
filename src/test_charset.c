/*
 *  test_charset.c  --  unit tests for charset.c and width.c
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 */

#include "../config.h"

#include <locale.h>
#include <stdio.h>
#include <string.h>

#include "charset.h"
#include "width.h"

static int failures;

#define FFFD "\xef\xbf\xbd"

static void check_bytes(const char *what, const char *got, size_t n,
                        const char *expect, size_t elen)
{
    if (n != elen || memcmp(got, expect, n) != 0) {
        printf("FAIL %s: got \"", what);
        for (size_t i = 0; i < n; ++i)
            printf((unsigned char)got[i] < 0x80 && got[i] >= ' '
                   ? "%c" : "\\x%02x", (unsigned char)got[i]);
        printf("\"\n");
        ++failures;
    }
}

/* decode 'in' from charset 'cs' to UTF-8 in chunks of 'chunk' bytes
 * with a flush at the end */
static void check_to_utf8(const char *what, enum charset cs,
                          const char *in, size_t inlen,
                          const char *expect, size_t elen)
{
    for (size_t chunk = 1; chunk <= 4; ++chunk) {
        struct utf8_decoder d = { .need = 0 };
        char out[256];
        size_t n = 0;
        for (size_t i = 0; i < inlen; i += chunk)
            for (size_t j = i; j < i + chunk && j < inlen; ++j) {
                int k = charset_decode_byte(cs, &d, in[j], out + n);
                if (k > CHARSET_DECODE_MAX) {
                    printf("FAIL %s: %d > CHARSET_DECODE_MAX\n", what, k);
                    ++failures;
                }
                n += k;
            }
        n += charset_decode_flush(&d, out + n);
        check_bytes(what, out, n, expect, elen);
    }
}

#define TO_UTF8(what, cs, in, expect) \
    check_to_utf8(what, cs, in, sizeof in - 1, expect, sizeof expect - 1)

#define ENCODE(what, cs, in, expect) do {                               \
        char out[256];                                                  \
        size_t n = charset_encode(cs, in, sizeof in - 1, out);          \
        check_bytes(what, out, n, expect, sizeof expect - 1);           \
    } while (0)

static void check_int(const char *what, long got, long expect)
{
    if (got != expect) {
        printf("FAIL %s: got %ld, expected %ld\n", what, got, expect);
        ++failures;
    }
}

static void test_utf8_internal(void)
{
    /* link charset -> UTF-8 */
    TO_UTF8("utf8 valid", CS_UTF8, "a\xc3\xa9\xe2\x82\xac\xf0\x9f\x98\x80",
            "a\xc3\xa9\xe2\x82\xac\xf0\x9f\x98\x80");
    TO_UTF8("utf8 stray", CS_UTF8, "a\x80z", "a" FFFD "z");
    TO_UTF8("utf8 overlong", CS_UTF8, "\xc0\xaf", FFFD FFFD);
    TO_UTF8("utf8 surrogate", CS_UTF8, "\xed\xa0\x80", FFFD FFFD FFFD);
    TO_UTF8("utf8 truncated+ascii", CS_UTF8, "\xe2\x82z", FFFD "z");
    TO_UTF8("utf8 truncated+stray", CS_UTF8, "\xe0\x80", FFFD FFFD);
    TO_UTF8("utf8 truncated at end", CS_UTF8, "ab\xf0\x9f", "ab" FFFD);
    TO_UTF8("utf8 0xff", CS_UTF8, "\xff", FFFD);
    TO_UTF8("latin1", CS_LATIN1, "caf\xe9 \xff\x80", "caf\xc3\xa9 \xc3\xbf\xc2\x80");
    TO_UTF8("ascii", CS_ASCII, "caf\xe9!", "caf" FFFD "!");

    char buf[64];
    size_t n = utf8_sanitize("ok\xc3\xa9\xe2x\xc3", 7, buf);
    check_bytes("sanitize", buf, n, "ok\xc3\xa9" FFFD "x" FFFD, 11);
    n = latin1_to_utf8("\xe9t\xe9", 3, buf);
    check_bytes("latin1_to_utf8", buf, n, "\xc3\xa9t\xc3\xa9", 5);

    /* UTF-8 -> link charset */
    ENCODE("encode utf8", CS_UTF8, "h\xc3\xa9\xe2\x82\xac", "h\xc3\xa9\xe2\x82\xac");
    ENCODE("encode latin1", CS_LATIN1, "h\xc3\xa9\xc3\xbf\xe2\x82\xac\xf0\x9f\x98\x80!",
           "h\xe9\xff??" "!");
    ENCODE("encode ascii", CS_ASCII, "h\xc3\xa9\xe2\x82\xac!", "h??" "!");
    ENCODE("encode latin1 invalid", CS_LATIN1, "a\x80" "b", "a?b");

    /* round trip of code points via utf8_put/utf8_get */
    for (uint32_t cp = 0; cp <= 0x10ffff; cp += cp < 0x800 ? 1 : 97) {
        char s[UTF8_MAX];
        uint32_t back;
        int len = utf8_put(cp, s);
        int got = utf8_get(s, len, &back);
        uint32_t expect = (cp >= 0xd800 && cp <= 0xdfff)
            ? UNICODE_REPLACEMENT : cp;
        if (got != len || back != expect) {
            printf("FAIL round trip U+%04X\n", (unsigned)cp);
            ++failures;
            break;
        }
    }
    uint32_t cp;
    check_int("utf8_get invalid len", utf8_get("\x80x", 2, &cp), 1);
    check_int("utf8_get invalid cp", cp, UNICODE_REPLACEMENT);
    check_int("utf8_get truncated", utf8_get("\xe2\x82", 2, &cp), 1);
    check_int("utf8_get FFFD", utf8_get(FFFD, 3, &cp), 3);
    check_int("utf8_get FFFD cp", cp, UNICODE_REPLACEMENT);
    check_int("utf8_put surrogate", utf8_put(0xd800, buf), 3);
    check_bytes("utf8_put surrogate", buf, 3, FFFD, 3);

    static const char s[] = "a\xc3\xa9\xe2\x82\xac\xf0\x9f\x98\x80z";
    const char *end = s + sizeof s - 1;
    check_int("utf8_prev 1", utf8_prev(s, end) - s, 10);
    check_int("utf8_prev 2", utf8_prev(s, s + 10) - s, 6);
    check_int("utf8_prev 3", utf8_prev(s, s + 6) - s, 3);
    check_int("utf8_prev 4", utf8_prev(s, s + 3) - s, 1);
    check_int("utf8_prev start", utf8_prev(s, s) - s, 0);
    static const char inv[] = "a\x80\x80";
    check_int("utf8_prev invalid", utf8_prev(inv, inv + 3) - inv, 2);
    check_int("utf8_count", utf8_count(s, sizeof s - 1), 5);
    check_int("seqlen ascii", utf8_seqlen('a'), 1);
    check_int("seqlen 2", utf8_seqlen(0xc3), 2);
    check_int("seqlen 3", utf8_seqlen(0xe2), 3);
    check_int("seqlen 4", utf8_seqlen(0xf0), 4);
    check_int("seqlen continuation", utf8_seqlen(0x80), 1);
    check_int("seqlen c0", utf8_seqlen(0xc0), 1);
    check_int("utf8_offset 2", utf8_offset(s, sizeof s - 1, 2), 3);
    check_int("utf8_offset past", utf8_offset(s, sizeof s - 1, 9), 11);
}

static void test_width(void)
{
    if (!setlocale(LC_CTYPE, "C.UTF-8")
        && !setlocale(LC_CTYPE, "en_US.UTF-8")) {
        printf("SKIP width tests: no UTF-8 locale\n");
        return;
    }
    check_int("width ascii", cp_width('a'), 1);
    check_int("width control", cp_width('\n'), 0);
    check_int("width e-acute", cp_width(0xe9), 1);
    check_int("width combining", cp_width(0x301), 0);
    check_int("width CJK", cp_width(0x4e2d), 2);
    check_int("utf8_width", utf8_width("a\xe4\xb8\xad\033[1mb\033[0m", 13), 4);

    /* "e" + U+0301, U+4E2D, "x" */
    static const char s[] = "e\xcc\x81\xe4\xb8\xadx";
    const char *end = s + sizeof s - 1;
    check_int("next cell 1", utf8_next_cell(s, end) - s, 3);
    check_int("next cell 2", utf8_next_cell(s + 3, end) - s, 6);
    check_int("next cell end", utf8_next_cell(end, end) - s, 7);
    check_int("prev cell 1", utf8_prev_cell(s, s + 6) - s, 3);
    check_int("prev cell 2", utf8_prev_cell(s, s + 3) - s, 0);
}

int main(void)
{
    /* UTF-8 validation: one U+FFFD per maximal invalid subpart */
    TO_UTF8("ascii", CS_UTF8, "hello", "hello");
    TO_UTF8("2-byte", CS_UTF8, "caf\xc3\xa9 \xc3\xbf", "caf\xc3\xa9 \xc3\xbf");
    TO_UTF8("U+0080", CS_UTF8, "\xc2\x80", "\xc2\x80");
    TO_UTF8("stray continuation", CS_UTF8, "a\x80\xbfz", "a" FFFD FFFD "z");
    TO_UTF8("invalid leads", CS_UTF8, "\xc0\xc1\xf5\xff", FFFD FFFD FFFD FFFD);
    TO_UTF8("overlong 3-byte", CS_UTF8, "\xe0\x80\xaf", FFFD FFFD FFFD);
    TO_UTF8("overlong 4-byte", CS_UTF8, "\xf0\x80\x80\xaf",
            FFFD FFFD FFFD FFFD);
    TO_UTF8("above U+10FFFF", CS_UTF8, "\xf4\x90\x80\x80",
            FFFD FFFD FFFD FFFD);
    TO_UTF8("max code point", CS_UTF8, "\xf4\x8f\xbf\xbf", "\xf4\x8f\xbf\xbf");
    TO_UTF8("truncated + lead", CS_UTF8, "\xe2\xc3\xa9", FFFD "\xc3\xa9");

    /* round trip of every ISO 8859-1 character */
    char all[256], enc[512], back[512];
    for (int i = 0; i < 255; ++i)
        all[i] = i + 1;
    size_t elen = latin1_to_utf8(all, 255, enc);
    if (charset_encode(CS_LATIN1, enc, elen, back) != 255
        || memcmp(back, all, 255) != 0) {
        printf("FAIL latin1 round trip\n");
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

    test_utf8_internal();
    test_width();

    if (failures == 0)
        printf("all charset tests passed\n");
    return failures != 0;
}
