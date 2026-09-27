/* public things from charset.c */

#ifndef _CHARSET_H_
#define _CHARSET_H_

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
