/* public things from charset.c */

#ifndef _CHARSET_H_
#define _CHARSET_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Internally, powwow uses UTF-8; these functions convert between that
 * and the encoding used on a remote link, and help handling UTF-8.
 */

/* replacement for characters that cannot be represented */
#define CHARSET_REPLACEMENT '?'

/* U+FFFD REPLACEMENT CHARACTER, for invalid input */
#define UTF8_REPLACEMENT     "\xef\xbf\xbd"
#define UTF8_REPLACEMENT_LEN 3
#define UNICODE_REPLACEMENT  0xfffd

#define ASCII_MAX   0x7f
#define LATIN1_MAX  0xff
#define UNICODE_MAX 0x10ffff    /* the last code point */

/* is 'cp' a C1 control character? */
static inline bool unicode_is_c1(uint32_t cp)
{
    return cp >= 0x80 && cp < 0xa0;
}

/* is 'cp' a UTF-16 surrogate, which is not a character? */
static inline bool unicode_is_surrogate(uint32_t cp)
{
    return cp >= 0xd800 && cp <= 0xdfff;
}

/* is 'cp' a high surrogate, the first of two in UTF-16? */
static inline bool unicode_is_high_surrogate(uint32_t cp)
{
    return cp >= 0xd800 && cp <= 0xdbff;
}

/* is 'cp' a low surrogate, the second of two in UTF-16? */
static inline bool unicode_is_low_surrogate(uint32_t cp)
{
    return cp >= 0xdc00 && cp <= 0xdfff;
}

/* is 'cp' a code point other than a surrogate, which UTF-8 can encode? */
static inline bool unicode_is_scalar(uint32_t cp)
{
    return cp <= UNICODE_MAX && !unicode_is_surrogate(cp);
}

/* the high surrogate of 'cp', above U+FFFF, in UTF-16 */
static inline uint32_t utf16_high(uint32_t cp)
{
    return 0xd800 + ((cp - 0x10000) >> 10);
}

/* the low surrogate of 'cp', above U+FFFF, in UTF-16 */
static inline uint32_t utf16_low(uint32_t cp)
{
    return 0xdc00 + ((cp - 0x10000) & 0x3ff);
}

/* the character of the surrogates 'high' and 'low' */
static inline uint32_t utf16_join(uint32_t high, uint32_t low)
{
    return 0x10000 + ((high - 0xd800) << 10) + (low - 0xdc00);
}

/* is 'b' a continuation byte of UTF-8, not the first of a character? */
static inline bool utf8_is_continuation(unsigned char b)
{
    return (b & 0xc0) == 0x80;
}

enum charset { CS_LATIN1, CS_UTF8, CS_ASCII };

struct utf8_decoder {
    uint32_t      cp;           /* code point being assembled */
    unsigned char lo, hi;       /* valid range for the next continuation */
    unsigned char need;         /* continuation bytes still expected */
};

const char *charset_name(enum charset cs);
/* look up an IANA charset name (case insensitive); returns -1 if unknown */
int charset_lookup(const char *name, size_t len);

void utf8_decoder_reset(struct utf8_decoder *d);
/*
 * Conversion between remote link charsets and UTF-8
 */

/* longest output of charset_decode_byte() */
#define CHARSET_DECODE_MAX (2 * UTF8_REPLACEMENT_LEN)

/* feed one byte in charset 'cs'; stores 0 to CHARSET_DECODE_MAX bytes
 * of UTF-8 in 'out' and returns how many; invalid input results in
 * U+FFFD; 'd' is only used for CS_UTF8 */
int charset_decode_byte(enum charset cs, struct utf8_decoder *d,
                        unsigned char b, char *out);
/* flush any incomplete UTF-8 sequence; stores 0 or
 * UTF8_REPLACEMENT_LEN bytes in 'out' */
int charset_decode_flush(struct utf8_decoder *d, char *out);
/* copy UTF-8 text, replacing invalid sequences by U+FFFD; 'dst' must
 * have room for 3 * len bytes; returns the number of bytes stored */
size_t utf8_sanitize(const char *src, size_t len, char *dst);
/* copy ISO 8859-1 text as UTF-8; 'dst' must have room for 2 * len
 * bytes; returns the number of bytes stored */
size_t latin1_to_utf8(const char *src, size_t len, char *dst);
/* encode 'len' bytes of UTF-8 text as 'cs' into 'dst', replacing
 * characters 'cs' cannot represent by CHARSET_REPLACEMENT; the result
 * is never longer than the input; returns the number of bytes stored */
size_t charset_encode(enum charset cs, const char *src, size_t len,
                      char *dst);

/*
 * UTF-8 helpers
 */

/* longest UTF-8 sequence */
#define UTF8_MAX 4

/* length of a UTF-8 sequence starting with 'lead'; 1 if invalid */
int utf8_seqlen(unsigned char lead);
/* decode the character at 's' (at most 'len' bytes); stores it in *cp
 * and returns its length (>= 1 if len > 0); an invalid sequence gives
 * U+FFFD and a length of 1 */
int utf8_get(const char *s, size_t len, uint32_t *cp);
/* encode 'cp' (U+FFFD if invalid) at 'dst'; returns its length */
int utf8_put(uint32_t cp, char *dst);
/* start of the character before 's', but not before 'start' */
const char *utf8_prev(const char *start, const char *s);
/* number of characters in 'len' bytes */
size_t utf8_count(const char *s, size_t len);
/* byte offset of character 'n', or 'len' if there are fewer */
size_t utf8_offset(const char *s, size_t len, size_t n);

#endif /* _CHARSET_H_ */
