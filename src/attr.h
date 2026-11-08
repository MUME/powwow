/* public things from attr.c */

#ifndef _ATTR_H_
#define _ATTR_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* effects; each is a bit in struct attr.flags */
enum attr_flag {
    ATTR_BOLD      = 1 << 0,
    ATTR_BLINK     = 1 << 1,
    ATTR_UNDERLINE = 1 << 2,
    ATTR_INVERSE   = 1 << 3,
    ATTR_DIM       = 1 << 4,
    ATTR_ITALIC    = 1 << 5,
    ATTR_HIDDEN    = 1 << 6,
    ATTR_STRIKE    = 1 << 7,
    ATTR_OVERLINE  = 1 << 8,
};

enum color_kind {
    COLOR_NONE,         /* no color change, use default */
    COLOR_NAMED,        /* one of the 16 named colors, 0-15 */
    COLOR_INDEX,        /* index 0-255 in the 256-color palette */
    COLOR_RGB           /* 24-bit 0xRRGGBB */
};

struct color {
    enum color_kind kind;
    uint32_t value;
};

/* colors and effects; all-zero means no attribute */
struct attr {
    unsigned flags;
    struct color fg, bg;
};

/* size of a buffer that can hold any attribute's escape sequence */
#define ATTR_SGR_MAX 80
/* size of a buffer that can hold any attribute's description */
#define ATTR_NAME_MAX 96

/*
 * parse a description such as "bold 208 on #202020" into *attr.
 * An empty description is no attribute. Return false if it is invalid.
 */
bool attr_parse(const char *text, struct attr *attr);

/* like attr_parse(), but if it is invalid, write why to 'why' of 'size' */
bool attr_parse_why(const char *text, struct attr *attr, char *why,
                    size_t size);

/* true if 'attr' changes nothing */
bool attr_is_none(const struct attr *attr);

/* write the escape sequence selecting 'attr' to 'buf', or "" */
void attr_sgr(const struct attr *attr, char *buf, size_t size);

/* write the description of 'attr' that attr_parse() reads back */
void attr_format(const struct attr *attr, char *buf, size_t size);

#endif /* _ATTR_H_ */
