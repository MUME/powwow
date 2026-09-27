/* public things from charset.c */

#ifndef _CHARSET_H_
#define _CHARSET_H_

#include <stddef.h>
#include <stdint.h>

/*
 * Internally, powwow uses ISO 8859-1; these functions convert between
 * that and the encoding used on a remote link.
 */

/* replacement for characters that cannot be represented */
#define CHARSET_REPLACEMENT '?'

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
/* feed one byte; stores 0-2 bytes in 'out' and returns how many */
int  utf8_decode_byte(struct utf8_decoder *d, unsigned char b, char *out);
/* flush any incomplete sequence; stores 0-1 bytes in 'out' */
int  utf8_decoder_flush(struct utf8_decoder *d, char *out);
/* decode a complete UTF-8 buffer in place; returns the new length */
size_t utf8_to_latin1(char *buf, size_t len);

/* encode 'len' bytes of ISO 8859-1 text as 'cs' into 'dst', which must
 * have room for 2 * len bytes; returns the number of bytes stored */
size_t charset_encode(enum charset cs, const char *src, size_t len,
                      char *dst);

#endif /* _CHARSET_H_ */
