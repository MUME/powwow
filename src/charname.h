/* look up characters by their Unicode names */

#ifndef _CHARNAME_H_
#define _CHARNAME_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* the file with the names, made by doc/unicode-names.sh */
extern char namesfile[];

/* called by charname_find() with each name found */
typedef void (*charname_fn)(const char *name, void *data);

bool charname_lookup(const char *name, size_t len, uint32_t *cp);
bool charname_starts_with(const char *prefix, const char *name);
bool charname_words_abbreviate(const char *pattern, const char *name);
bool charname_words_match(const char *pattern, const char *name,
                          bool anchored);
int charname_find(const char *pattern, charname_fn fn, void *data);

#endif /* _CHARNAME_H_ */
