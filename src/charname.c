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

#include "defines.h"
#include "charname.h"

char namesfile[BUFSIZE];
char emojifile[BUFSIZE];

/* an entry of a file of names */
struct name_entry {
    uint32_t cps[CHARNAME_MAX];
    int ncps;
    char name[128];
    char group[128];            /* the group and subgroup of an emoji */
    char words[256];            /* its group, and then its name */
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

/*
 * does each word of 'pattern' match the start of a word of 'name', in
 * order, and the first word the start of 'name' if 'anchored'? case, and
 * the separators between words, do not matter
 */
bool charname_words_match(const char *pattern, const char *name,
                          bool anchored)
{
    const char *n = name;
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
            if (*n == '\0' || (anchored && w == pattern && n > name))
                return false;
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
        n += len;
        w += len;
    }
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
    return (cp >= 0x20 && cp != 0x7f && (cp < 0x80 || cp >= 0xa0)
            && (cp < 0xd800 || cp > 0xdfff) && cp <= 0x10ffff);
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
static bool read_entry(FILE *f, struct name_entry *e)
{
    char line[512];
    while (fgets(line, sizeof(line), f) != NULL) {
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
        char group[64], subgroup[64];
        rest = copy_field(rest, group, sizeof(group));
        copy_field(rest, subgroup, sizeof(subgroup));
        e->group[0] = '\0';
        if (group[0] != '\0')
            snprintf(e->group, sizeof(e->group), "%s, %s", group, subgroup);
        snprintf(e->words, sizeof(e->words), "%s %s %s", group, subgroup,
                 e->name);
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

    FILE *f = fopen(file, "r");
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
    fclose(f);
    for (int i = 0; i < found; ++i) {
        if (!is_typable(cps[i]))
            return 0;
    }
    return found;
}

/*
 * call 'fn' with each name in 'file' that 'pattern' matches as in
 * charname_words_match(), where the words of the group of an emoji come
 * before its name, with the group if the name does not match by itself,
 * or else NULL, and 'data'; return how many there are, or -1 if the names
 * cannot be read
 */
int charname_find(const char *file, const char *pattern, charname_fn fn,
                  void *data)
{
    FILE *f = fopen(file, "r");
    if (f == NULL)
        return -1;
    struct name_entry e;
    int count = 0;
    while (read_entry(f, &e)) {
        if (!charname_words_match(pattern, e.words, false))
            continue;
        fn(e.name,
           charname_words_match(pattern, e.name, false) ? NULL : e.group,
           data);
        ++count;
    }
    fclose(f);
    return count;
}
