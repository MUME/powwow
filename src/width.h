/* public things from width.c */

#ifndef _WIDTH_H_
#define _WIDTH_H_

#include <stddef.h>
#include <stdint.h>

/* number of terminal columns used to display 'cp' */
int cp_width(uint32_t cp);
/* number of terminal columns used to display 'len' bytes of UTF-8,
 * skipping ANSI escape sequences */
int utf8_width(const char *s, size_t len);

/*
 * A "cell" is a character followed by any combining characters; the
 * line editor moves and deletes one cell at a time.
 */
/* start of the next cell after 's', but not after 'end' */
const char *utf8_next_cell(const char *s, const char *end);
/* start of the cell before 's', but not before 'start' */
const char *utf8_prev_cell(const char *start, const char *s);

#endif /* _WIDTH_H_ */
