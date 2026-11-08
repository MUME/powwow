/* look up characters by their Unicode names */

#ifndef _CHARNAME_H_
#define _CHARNAME_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* the files with the names, made by doc/unicode-names.sh and
 * doc/emoji-names.sh */
extern char namesfile[], emojifile[];

/* the code points of an emoji sequence, at most */
enum { CHARNAME_MAX = 16 };

/* called by charname_find() with each name found, and its group if the
 * name matches only with it */
typedef void (*charname_fn)(const char *name, const char *group, void *data);

int charname_lookup(const char *file, const char *name, size_t len,
                    uint32_t *cps);
bool charname_starts_with(const char *prefix, const char *name);
bool charname_words_abbreviate(const char *pattern, const char *name);
bool charname_words_match(const char *pattern, const char *name,
                          bool anchored);
int charname_find(const char *file, const char *pattern, charname_fn fn,
                  void *data);

#endif /* _CHARNAME_H_ */
