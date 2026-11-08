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

/* does 'name' start with the 'len' characters of 'prefix'? */
static bool name_starts_with(const char *name, const char *prefix,
                             size_t len)
{
    for (size_t i = 0; i < len; ++i) {
        if (name[i] == '\0' || name_char(name[i]) != name_char(prefix[i]))
            return false;
    }
    return true;
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
        if (strlen(entry) == len && name_starts_with(entry, name, len))
            found = c;
    }
    fclose(f);
    if (found < 0 || !is_typable(found))
        return false;
    *cp = found;
    return true;
}

/*
 * find the names that start with 'prefix': return how many there are, and
 * copy the 'n'th one into 'name', and their longest common prefix into
 * 'common', both of 'size' bytes; return -1 if the names cannot be read
 */
int charname_complete(const char *prefix, int n, char *name, char *common,
                      size_t size)
{
    FILE *f = fopen(namesfile, "r");
    if (f == NULL)
        return -1;
    size_t len = strlen(prefix);
    char entry[128];
    int count = 0;
    *name = *common = '\0';
    while (read_name(f, entry, sizeof(entry)) >= 0) {
        if (!name_starts_with(entry, prefix, len))
            continue;
        if (count == n)
            snprintf(name, size, "%s", entry);
        if (count == 0) {
            snprintf(common, size, "%s", entry);
        } else {
            size_t i = 0;
            while (common[i] != '\0' && common[i] == entry[i])
                ++i;
            common[i] = '\0';
        }
        ++count;
    }
    fclose(f);
    return count;
}
