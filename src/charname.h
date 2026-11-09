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

/* called by charname_find() with each name found, its 'ncps' code points
 * 'cps', and its group if the name matches only with it */
typedef void (*charname_fn)(const char *name, const uint32_t *cps,
                            int ncps, const char *group, void *data);

int charname_lookup(const char *file, const char *name, size_t len,
                    uint32_t *cps);
bool charname_starts_with(const char *prefix, const char *name);
bool charname_words_abbreviate(const char *pattern, const char *name);
bool charname_words_match(const char *pattern, const char *name,
                          bool anchored);
bool charname_is_sep(char c);
size_t charname_name_start(const char *pattern, const char *name);
int charname_find(const char *file, const char *group, const char *subgroup,
                  const char *pattern, charname_fn fn, void *data);

/* the words of a pattern that can be aligned, and a name, at most */
enum { CHARNAME_WORDS_MAX = 16, CHARNAME_LEN = 128 };

/*
 * a part of the names that a pattern matches, aligned with its words: the
 * same in all of them, or else 'differs', and then the separators and
 * whole words at its start and end that are, 'prefix' and 'suffix' bytes
 * of 'text', the part of the first name; the end of the names after the
 * last word has 'prefix' bytes in common
 */
struct charname_part {
    bool differs;
    char text[CHARNAME_LEN];
    size_t prefix, suffix;
};

/*
 * the names that a pattern matches, aligned with its words: the gaps
 * before each word, the first unused, and the end, and the words of the
 * names that they match; 'failed' if they cannot be aligned
 */
struct charname_align {
    int names, count;
    bool failed;
    struct charname_part gaps[CHARNAME_WORDS_MAX + 1];
    struct charname_part words[CHARNAME_WORDS_MAX];
};

/* add 'name', which 'pattern' matches as in charname_words_match() */
void charname_align_add(struct charname_align *align, const char *pattern,
                        const char *name);
/*
 * the text that 'pattern' can be completed to, from the names in 'align',
 * into 'out' of 'size': each word as the whole word that it matches in all
 * of them, else as typed, and the words between them and at the end that
 * all have; '*cursor' at the end, unless the names end the same, and then
 * after the first word that differs, or between two separators where the
 * first words between two words differ; false if there are no names
 */
bool charname_align_text(const struct charname_align *align,
                         const char *pattern, char *out, size_t size,
                         size_t *cursor);

/* called by charname_groups() with each group and subgroup */
typedef void (*charname_group_fn)(const char *group, const char *subgroup,
                                  int count, void *data);

void charname_groups(const char *file, charname_group_fn fn, void *data);

#endif /* _CHARNAME_H_ */
