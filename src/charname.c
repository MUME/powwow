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

/*
 * the character 'c' of a name, for comparing names: case, and spaces,
 * hyphens and underscores do not matter
 */
static char name_char(char c)
{
    if (c == '-' || c == '_')
        return ' ';
    return toupper((unsigned char)c);
}

/* is 'c' between words of a name? */
static bool is_name_sep(char c)
{
    return c == ' ' || c == '-' || c == '_';
}

/*
 * does each word of 'pattern' match the start of a word of 'name', in
 * order, and the first word the start of 'name' if 'anchored'? case, and
 * spaces, hyphens and underscores do not matter
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

/*
 * read the next name from 'f' into 'name': return its code point, or -1 at
 * the end of the file
 */
static long read_name(FILE *f, char *name, size_t size)
{
    char line[256];
    while (fgets(line, sizeof(line), f) != NULL) {
        char *end;
        long cp = strtol(line, &end, 16);
        if (line[0] == '#' || end == line || *end != ' ')
            continue;
        size_t len = strcspn(end + 1, "\n");
        if (len >= size)
            len = size - 1;
        memcpy(name, end + 1, len);
        name[len] = '\0';
        return cp;
    }
    return -1;
}

/*
 * find the character with the Unicode name or code point in the 'len'
 * bytes of 'name'; return false if there is none, or it cannot be typed
 */
bool charname_lookup(const char *name, size_t len, uint32_t *cp)
{
    if (parse_code_point(name, len, cp))
        return is_typable(*cp);

    FILE *f = fopen(namesfile, "r");
    if (f == NULL)
        return false;
    char entry[128];
    long found = -1;
    for (long c; found < 0 && (c = read_name(f, entry, sizeof(entry))) >= 0; ) {
        if (same_name(name, len, entry))
            found = c;
    }
    fclose(f);
    if (found < 0 || !is_typable(found))
        return false;
    *cp = found;
    return true;
}

/*
 * call 'fn' with each name, in lowercase, that 'pattern' matches as in
 * charname_words_match(), and 'data'; return how many there are, or -1 if
 * the names cannot be read
 */
int charname_find(const char *pattern, charname_fn fn, void *data)
{
    FILE *f = fopen(namesfile, "r");
    if (f == NULL)
        return -1;
    char entry[128];
    int count = 0;
    while (read_name(f, entry, sizeof(entry)) >= 0) {
        if (!charname_words_match(pattern, entry, false))
            continue;
        for (char *p = entry; *p != '\0'; ++p)
            *p = tolower((unsigned char)*p);
        fn(entry, data);
        ++count;
    }
    fclose(f);
    return count;
}
