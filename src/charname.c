/*
 *  charname.c  --  look up characters by their Unicode names
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 */

#include "../config.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef HAVE_ZLIB
#  include <zlib.h>
#endif

#include "charset.h"
#include "defines.h"
#include "charname.h"

char namesfile[BUFSIZE];
char emojifile[BUFSIZE];

/*
 * a file of names, read through zlib if powwow has it, as the files may
 * be installed compressed by gzip; zlib reads uncompressed files too
 */
#ifdef HAVE_ZLIB
typedef gzFile names_file;
#else
typedef FILE *names_file;
#endif

static names_file names_open(const char *file)
{
#ifdef HAVE_ZLIB
    return gzopen(file, "r");
#else
    return fopen(file, "r");
#endif
}

static char *names_gets(char *line, int size, names_file f)
{
#ifdef HAVE_ZLIB
    return gzgets(f, line, size);
#else
    return fgets(line, size, f);
#endif
}

static void names_close(names_file f)
{
#ifdef HAVE_ZLIB
    gzclose(f);
#else
    fclose(f);
#endif
}

/* an entry of a file of names */
struct name_entry {
    uint32_t cps[CHARNAME_MAX];
    int ncps;
    char name[128];
    char group[64], subgroup[64];       /* of an emoji */
    char groups[64 + 2 + 64];           /* both, as "group, subgroup" */
    char words[256];                    /* both, and then the name */
};

/* is 'c' between words of a name? */
static bool is_name_sep(char c)
{
    return (c == ' ' || c == '-' || c == '_' || c == ':' || c == ','
            || c == '.');
}

/*
 * the character 'c' of a name, for comparing names: case, and the
 * separators between words, do not matter
 */
static char name_char(char c)
{
    if (is_name_sep(c))
        return ' ';
    return toupper((unsigned char)c);
}

/* where a word of a pattern matched in a name: the whole word */
struct word_span {
    size_t start, end;
};

/*
 * match each word of 'pattern' with the start of a word of 'name', in
 * order, the first word with the start of 'name' if 'anchored', and each
 * with the first word that it can, ignoring case; return how many words
 * 'pattern' has, or -1 if it does not match, or has more than 'max' words
 * when 'spans' is not NULL, which then gets the words of 'name' matched
 */
static int match_words(const char *pattern, const char *name, bool anchored,
                       struct word_span *spans, int max)
{
    const char *n = name;
    int count = 0;
    for (const char *w = pattern; *w != '\0'; ) {
        if (is_name_sep(*w)) {
            ++w;
            continue;
        }
        size_t len = 0;
        while (w[len] != '\0' && !is_name_sep(w[len]))
            ++len;
        /* find a word of the name, from 'n' on, that starts with 'w' */
        for (;; ++n) {
            if (*n == '\0' || (anchored && count == 0 && n > name))
                return -1;
            if ((n == name || is_name_sep(n[-1])) && !is_name_sep(*n)) {
                size_t i = 0;
                while (i < len && n[i] != '\0'
                       && toupper((unsigned char)n[i])
                          == toupper((unsigned char)w[i]))
                    ++i;
                if (i == len)
                    break;
            }
        }
        if (spans != NULL) {
            if (count == max)
                return -1;
            size_t end = n - name;
            while (name[end] != '\0' && !is_name_sep(name[end]))
                ++end;
            spans[count] = (struct word_span){
                .start = n - name, .end = end
            };
        }
        ++count;
        n += len;
        w += len;
    }
    return count;
}

/*
 * does each word of 'pattern' match the start of a word of 'name', in
 * order, and the first word the start of 'name' if 'anchored'? case, and
 * the separators between words, do not matter
 */
bool charname_words_match(const char *pattern, const char *name,
                          bool anchored)
{
    return match_words(pattern, name, anchored, NULL, 0) >= 0;
}

/* is 'c' a separator between the words of a name? */
bool charname_is_sep(char c)
{
    return is_name_sep(c);
}

/*
 * where the words of 'pattern' start that match 'name' by themselves,
 * after those that match only the group and subgroup of an emoji
 */
size_t charname_name_start(const char *pattern, const char *name)
{
    size_t at = 0;
    while (pattern[at] != '\0'
           && !charname_words_match(pattern + at, name, false)) {
        while (pattern[at] != '\0' && !is_name_sep(pattern[at]))
            ++at;
        while (is_name_sep(pattern[at]))
            ++at;
    }
    return at;
}

/* are the 'len' bytes of 'a' and 'b' the same, ignoring case? */
static bool same_text(const char *a, const char *b, size_t len)
{
    for (size_t i = 0; i < len; ++i) {
        if (toupper((unsigned char)a[i]) != toupper((unsigned char)b[i]))
            return false;
    }
    return true;
}

/* set 'part' to the 'len' bytes of 'text', if it is the first name */
static void part_set(struct charname_part *part, const char *text,
                     size_t len)
{
    if (len >= sizeof(part->text))
        len = sizeof(part->text) - 1;
    memcpy(part->text, text, len);
    part->text[len] = '\0';
    part->prefix = part->suffix = len;
}

/*
 * merge the gap between two words, of 'len' bytes at 'gap', into 'part':
 * if they differ, keep the whole words that both start and end with
 */
static void gap_merge(struct charname_part *part, const char *gap,
                      size_t len)
{
    size_t plen = strlen(part->text);
    if (!part->differs && len == plen && same_text(part->text, gap, len))
        return;
    part->differs = true;
    size_t max = len < plen ? len : plen;
    size_t prefix = 0;
    for (size_t i = 0; i < part->prefix && i < max
             && toupper((unsigned char)part->text[i])
                == toupper((unsigned char)gap[i]); ++i) {
        if (is_name_sep(part->text[i]))
            prefix = i + 1;
    }
    size_t suffix = 0;
    for (size_t i = 1; i <= part->suffix && i <= max - prefix
             && toupper((unsigned char)part->text[plen - i])
                == toupper((unsigned char)gap[len - i]); ++i) {
        if (is_name_sep(part->text[plen - i]))
            suffix = i;
    }
    part->prefix = prefix;
    part->suffix = suffix;
}

void charname_align_add(struct charname_align *align, const char *pattern,
                        const char *name)
{
    struct word_span spans[CHARNAME_WORDS_MAX];
    int count = match_words(pattern, name, false, spans, CHARNAME_WORDS_MAX);
    if (count <= 0 || (align->names > 0 && count != align->count)) {
        align->failed = true;
        return;
    }
    bool first = align->names++ == 0;
    align->count = count;
    for (int i = 0; i <= count; ++i) {
        /* the gap before the word, or the end after the last one */
        size_t start = i > 0 ? spans[i - 1].end : 0;
        size_t end = i < count ? spans[i].start : strlen(name);
        if (first)
            part_set(&align->gaps[i], name + start, end - start);
        else if (i < count)
            gap_merge(&align->gaps[i], name + start, end - start);
        else {
            /* the end: as far as they have it in common */
            struct charname_part *tail = &align->gaps[count];
            size_t j = 0;
            while (j < tail->prefix && name[start + j] != '\0'
                   && name_char(name[start + j]) == name_char(tail->text[j]))
                ++j;
            if (j < tail->prefix || strlen(tail->text) != end - start)
                tail->differs = true;
            tail->prefix = j;
        }
        if (i == count)
            break;
        struct charname_part *word = &align->words[i];
        size_t len = spans[i].end - spans[i].start;
        if (first)
            part_set(word, name + spans[i].start, len);
        else if (strlen(word->text) != len
                 || !same_text(word->text, name + spans[i].start, len))
            word->differs = true;
    }
}

/* append the 'len' bytes of 'text' to 'out', of 'size', at '*n' */
static void append(char *out, size_t size, size_t *n, const char *text,
                   size_t len)
{
    if (*n + len >= size)
        len = size - 1 - *n;
    memcpy(out + *n, text, len);
    *n += len;
    out[*n] = '\0';
}

bool charname_align_text(const struct charname_align *align,
                         const char *pattern, char *out, size_t size,
                         size_t *cursor)
{
    if (align->failed || align->names == 0 || size == 0)
        return false;
    /* where the cursor goes: the first gap or word where the names
     * differ, unless they also differ at the end */
    int gap_at = -1, word_at = -1;
    for (int i = 0; !align->gaps[align->count].differs && i < align->count;
         ++i) {
        if (i > 0 && align->gaps[i].differs) {
            gap_at = i;
            break;
        }
        if (align->words[i].differs) {
            word_at = i;
            break;
        }
    }
    size_t n = 0;
    out[0] = '\0';
    *cursor = SIZE_MAX;
    const char *w = pattern;
    for (int i = 0; i <= align->count; ++i) {
        const struct charname_part *gap = &align->gaps[i];
        if (i == align->count) {
            /* the end: as far as the names have it in common, or the
             * separators typed after the last word, if longer */
            size_t typed = strlen(w);
            if (gap->prefix >= typed)
                append(out, size, &n, gap->text, gap->prefix);
            else
                append(out, size, &n, w, typed);
            break;
        }
        const char *suffix = gap->text + strlen(gap->text) - gap->suffix;
        if (!gap->differs) {
            append(out, size, &n, gap->text, strlen(gap->text));
        } else if (i == gap_at) {
            /* the words that differ go between two separators, where the
             * cursor goes to type them */
            if (gap->prefix > 0)
                append(out, size, &n, gap->text, gap->prefix);
            else
                append(out, size, &n, " ", 1);
            *cursor = n;
            if (gap->suffix > 0)
                append(out, size, &n, suffix, gap->suffix);
            else
                append(out, size, &n, " ", 1);
        } else if (i > 0) {
            /* the words in common, between single separators */
            if (gap->prefix > 0)
                append(out, size, &n, gap->text, gap->prefix);
            if (gap->suffix > 0 && gap->prefix > 0)
                append(out, size, &n, suffix + 1, gap->suffix - 1);
            else if (gap->suffix > 0)
                append(out, size, &n, suffix, gap->suffix);
            else if (gap->prefix == 0)
                append(out, size, &n, " ", 1);
        }
        /* the word typed, or the whole word that it stands for */
        while (*w != '\0' && is_name_sep(*w))
            ++w;
        size_t len = 0;
        while (w[len] != '\0' && !is_name_sep(w[len]))
            ++len;
        const struct charname_part *word = &align->words[i];
        if (word->differs)
            append(out, size, &n, w, len);
        else
            append(out, size, &n, word->text, strlen(word->text));
        if (i == word_at)
            *cursor = n;
        w += len;
    }
    if (*cursor == SIZE_MAX)
        *cursor = n;
    return true;
}

/* does 'name' start with 'prefix'? case, and the separators, do not matter */
bool charname_starts_with(const char *prefix, const char *name)
{
    for (; *prefix != '\0'; ++prefix, ++name) {
        if (*name == '\0' || name_char(*prefix) != name_char(*name))
            return false;
    }
    return true;
}

/*
 * does each word of 'pattern' match the start of the word of 'name' in
 * the same place, and 'name' have no more words? case, and the
 * separators, do not matter
 */
bool charname_words_abbreviate(const char *pattern, const char *name)
{
    const char *w = pattern, *n = name;
    for (;;) {
        while (is_name_sep(*w))
            ++w;
        while (is_name_sep(*n))
            ++n;
        if (*w == '\0' || *n == '\0')
            return *w == '\0' && *n == '\0';
        for (; *w != '\0' && !is_name_sep(*w); ++w, ++n) {
            if (toupper((unsigned char)*w) != toupper((unsigned char)*n))
                return false;
        }
        while (*n != '\0' && !is_name_sep(*n))
            ++n;
    }
}

/* are the 'len' bytes of 'a' the same name as 'b'? */
static bool same_name(const char *a, size_t len, const char *b)
{
    for (size_t i = 0; i < len; ++i) {
        if (b[i] == '\0' || name_char(a[i]) != name_char(b[i]))
            return false;
    }
    return b[len] == '\0';
}

/* is 'cp' a character that can be typed, not a control or a surrogate? */
static bool is_typable(uint32_t cp)
{
    return (cp >= 0x20 && cp != ASCII_DEL && (cp < 0x80 || cp >= 0xa0)
            && unicode_is_scalar(cp));
}

/*
 * parse a code point written as U+XXXX, #xXXXX or XXXX in hex; return
 * false if 'text' is not one
 */
static bool parse_code_point(const char *text, size_t len, uint32_t *cp)
{
    if (len > 2 && (text[0] == 'U' || text[0] == 'u') && text[1] == '+') {
        text += 2;
        len -= 2;
    } else if (len > 2 && text[0] == '#' && (text[1] == 'x' || text[1] == 'X')) {
        text += 2;
        len -= 2;
    }
    if (len == 0 || len > 6)
        return false;
    uint32_t value = 0;
    for (size_t i = 0; i < len; ++i) {
        if (!isxdigit((unsigned char)text[i]))
            return false;
        value = value * 16 + (isdigit((unsigned char)text[i])
                              ? text[i] - '0'
                              : toupper((unsigned char)text[i]) - 'A' + 10);
    }
    *cp = value;
    return true;
}

/* copy the field of 'p' up to a tab or the end of the line into 'dst' */
static const char *copy_field(const char *p, char *dst, size_t size)
{
    size_t len = strcspn(p, "\t\n");
    snprintf(dst, size, "%.*s", (int)len, p);
    p += len;
    return *p == '\t' ? p + 1 : p;
}

/*
 * read the next entry from 'f': the code points in hex, a tab, the name,
 * and for an emoji, a tab, its group, a tab, and its subgroup; return
 * false at the end of the file
 */
static bool read_entry(names_file f, struct name_entry *e)
{
    char line[512];
    while (names_gets(line, sizeof(line), f) != NULL) {
        if (line[0] == '#')
            continue;
        char *p = line;
        e->ncps = 0;
        while (e->ncps < CHARNAME_MAX && isxdigit((unsigned char)*p)) {
            e->cps[e->ncps++] = strtoul(p, &p, 16);
            while (*p == ' ')
                ++p;
        }
        if (e->ncps == 0 || *p != '\t')
            continue;
        const char *rest = copy_field(p + 1, e->name, sizeof(e->name));
        rest = copy_field(rest, e->group, sizeof(e->group));
        copy_field(rest, e->subgroup, sizeof(e->subgroup));
        e->groups[0] = '\0';
        if (e->group[0] != '\0')
            snprintf(e->groups, sizeof(e->groups), "%s, %s", e->group,
                     e->subgroup);
        snprintf(e->words, sizeof(e->words), "%s %s %s", e->group,
                 e->subgroup, e->name);
        return true;
    }
    return false;
}

/*
 * find the character or emoji with the name, or code point, in the 'len'
 * bytes of 'name' in 'file', and copy its code points into 'cps', of
 * CHARNAME_MAX; return how many there are, or 0 if there is none, or it
 * cannot be typed
 */
int charname_lookup(const char *file, const char *name, size_t len,
                    uint32_t *cps)
{
    if (parse_code_point(name, len, cps))
        return is_typable(cps[0]) ? 1 : 0;

    names_file f = names_open(file);
    if (f == NULL)
        return 0;
    struct name_entry e;
    int found = 0;
    while (found == 0 && read_entry(f, &e)) {
        if (same_name(name, len, e.name)) {
            found = e.ncps;
            memcpy(cps, e.cps, found * sizeof(*cps));
        }
    }
    names_close(f);
    for (int i = 0; i < found; ++i) {
        if (!is_typable(cps[i]))
            return 0;
    }
    return found;
}

/*
 * call 'fn' with each name in 'file' that 'pattern' matches as in
 * charname_words_match(), with its code points, the group and subgroup of
 * an emoji if the name matches only with them, or else NULL, and 'data';
 * the words of the group and subgroup come before those of the name,
 * unless 'group' is not NULL: then only the emoji are found whose group
 * and subgroup match the words of 'group' and 'subgroup'; return how
 * many there are, or -1 if the names cannot be read
 */
int charname_find(const char *file, const char *group, const char *subgroup,
                  const char *pattern, charname_fn fn, void *data)
{
    names_file f = names_open(file);
    if (f == NULL)
        return -1;
    struct name_entry e;
    int count = 0;
    while (read_entry(f, &e)) {
        bool by_name = charname_words_match(pattern, e.name, false);
        if (group != NULL
            ? (!by_name || !charname_words_match(group, e.group, false)
               || !charname_words_match(subgroup, e.subgroup, false))
            : !by_name && !charname_words_match(pattern, e.words, false))
            continue;
        fn(e.name, e.cps, e.ncps, by_name ? NULL : e.groups, data);
        ++count;
    }
    names_close(f);
    return count;
}

/*
 * call 'fn' with each group and subgroup of the emoji in 'file', with how
 * many emoji there are in it, and 'data'
 */
void charname_groups(const char *file, charname_group_fn fn, void *data)
{
    names_file f = names_open(file);
    if (f == NULL)
        return;
    struct name_entry e;
    char group[64] = "", subgroup[64] = "";
    int count = 0;
    while (read_entry(f, &e)) {
        if (e.group[0] == '\0')
            break;              /* characters, not emoji */
        if (strcmp(e.group, group) != 0 || strcmp(e.subgroup, subgroup) != 0) {
            if (count > 0)
                fn(group, subgroup, count, data);
            strcpy(group, e.group);
            strcpy(subgroup, e.subgroup);
            count = 0;
        }
        ++count;
    }
    if (count > 0)
        fn(group, subgroup, count, data);
    names_close(f);
}
