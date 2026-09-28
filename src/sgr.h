/* public things from sgr.c */

#ifndef _SGR_H_
#define _SGR_H_

#include <stddef.h>

/*
 * The graphic rendition (colors, bold, ...) in effect after some text,
 * as the SGR escape sequences ("ESC [ ... m") seen since the last reset.
 */
#define SGR_STATE_MAX 256

struct sgr_state {
    char   seq[SGR_STATE_MAX];  /* NUL-terminated sequences */
    size_t len;
    int    reset;               /* a reset preceded the sequences */
};

/* initialize to no sequences and no reset */
void sgr_reset(struct sgr_state *st);
/* update 'st' with the SGR sequences in 'len' bytes of text */
void sgr_apply(struct sgr_state *st, const char *s, size_t len);

#endif /* _SGR_H_ */
