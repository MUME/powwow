/* public things from width.c */

#ifndef _WIDTH_H_
#define _WIDTH_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* number of terminal columns used to display 'cp' */
int cp_width(uint32_t cp);
/* number of terminal columns used to display 'len' bytes of UTF-8,
 * skipping ANSI escape sequences */
int utf8_width(const char *s, size_t len);

/*
 * A "cell" is what the terminal draws as a unit: a grapheme cluster,
 * such as a character with combining characters, a flag, or emoji
 * joined by ZWJ.  Without libgrapheme, it is a character followed by
 * any combining characters.  An ASCII control character is always a
 * cell of its own.  The line editor moves and deletes one cell at a
 * time.
 */
/* a cell being read one character at a time */
struct cell_state {
    uint32_t first, prev;       /* its first and last characters */
    uint_least16_t gstate;      /* libgrapheme's state */
    int width;                  /* in terminal columns */
};
/* start a cell with 'cp' */
void cell_start(struct cell_state *c, uint32_t cp);
/* add 'cp' to the cell if it belongs to it (no grapheme cluster break
 * before it), and return whether it does; otherwise, 'cp' starts a new
 * cell and 'c' is unchanged */
bool cell_add(struct cell_state *c, uint32_t cp);
/* length of the cell at 's', at most 'len' bytes (>= 1 if len > 0) */
size_t utf8_cell_len(const char *s, size_t len);
/* number of terminal columns used to display the cell of 'len' bytes
 * at 's' */
int utf8_cell_width(const char *s, size_t len);
/* start of the next cell after 's', but not after 'end' */
const char *utf8_next_cell(const char *s, const char *end);
/* start of the cell before 's', but not before 'start' */
const char *utf8_prev_cell(const char *start, const char *s);

#endif /* _WIDTH_H_ */
