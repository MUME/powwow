/*
 *  utils.c  --  miscellaneous utility functions
 *
 *  Copyright (C) 1998,2002 by Massimiliano Ghilardi
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 */

#include "defines.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdarg.h>
#include <ctype.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>

#include "main.h"
#include "utils.h"
#include "list.h"
#include "cmd.h"
#include "cmd2.h"
#include "beam.h"
#include "tty.h"
#include "edit.h"
#include "eval.h"
#include "log.h"
#include "tls.h"
#include "charset.h"
#include "width.h"
#include "sgr.h"
#include "charname.h"

enum {
    SAVEFILEVER = 9,            /* current version */
    UTF8_SAVEFILEVER = 8,       /* version when files became UTF-8 */
};

static bool can_suspend = false; /* true if shell has job control */

/*
 * restore the terminal, print that memory ran out and terminate, without
 * saving the settings; it allocates nothing
 */
noreturn void fatal_out_of_memory(void)
{
    clear_input_line(opt_compact);
    if (!opt_compact)
        tty_putc('\n');
    tty_flush();
    fputs("#fatal error: out of memory\n", stderr);
    tty_puts("#settings NOT saved to file.\n");
    tty_quit();
    exit(EXIT_FAILURE);
}

/* malloc(), but at least 1 byte, and fatal_out_of_memory() if it fails */
void *xmalloc(size_t size)
{
    void *p = malloc(size != 0 ? size : 1);
    if (p == NULL)
        fatal_out_of_memory();
    return p;
}

/* calloc(), like xmalloc() */
void *xcalloc(size_t count, size_t size)
{
    void *p = calloc(count != 0 ? count : 1, size != 0 ? size : 1);
    if (p == NULL)
        fatal_out_of_memory();
    return p;
}

/* realloc(), like xmalloc() */
void *xrealloc(void *p, size_t size)
{
    void *res = realloc(p, size != 0 ? size : 1);
    if (res == NULL)
        fatal_out_of_memory();
    return res;
}

/* strdup(), like xmalloc() */
char *xstrdup(const char *s)
{
    size_t size = strlen(s) + 1;
    return memcpy(xmalloc(size), s, size);
}

/* xstrdup(), or NULL for NULL */
char *my_strdup(const char *s)
{
    return s != NULL ? xstrdup(s) : NULL;
}

/*
 * non-braindamaged strncpy:
 * copy up to len chars from src to dst, then add a final \0
 * (i.e. dst[len] = '\0')
 */
char *my_strncpy(char *dst, const char *src, int len)
{
    int slen = strlen(src);
    if (slen < len)
        return strcpy(dst, src);
    memcpy(dst, src, len);
    dst[len] = '\0';
    return dst;
}

/*
 * Determine the printed length of a string. This can be less than the string
 * length since it might contain escape sequences. Treated as such are
 * "<esc> [ <non-letters> <letter>", "<esc> <non-[>", "<control-char>".
 * This is not entirely universal but covers the most common cases (i.e. ansi)
 */
int printstrlen(char *s)
{
    int l = 0;
    enum { NORM, ESCAPE, BRACKET } state = NORM;
    const char *end = s + strlen(s);
    while (*s != '\0') {
        uint32_t cp;
        size_t n = utf8_cell_len(s, end - s);
        utf8_get(s, n, &cp);
        int w = utf8_cell_width(s, n);
        s += n;
        switch (state) {
        case NORM:
            if (cp == ASCII_ESC) {
                state = ESCAPE;
            } else if (cp == '\b') {
                if (l > 0)
                    --l;
            } else if (cp == '\r') {
                l = (l / cols) * cols;
            } else {
                l += w;
            }
            break;

        case ESCAPE:
            state = (cp == '[') ? BRACKET : NORM;
            break;

        case BRACKET:
            if (is_csi_final(cp))
                state = NORM;
            break;
        }
    }
    return l;
}

/*
 * fgets() for UTF-8 text files: invalid UTF-8 is replaced by U+FFFD,
 * truncating the line at a character boundary if it no longer fits
 */
char *utf8_fgets(char *buf, int size, FILE *f)
{
    if (fgets(buf, size, f) == NULL)
        return NULL;

    /* turn a CR LF line ending, as on Windows, into LF */
    size_t len = strlen(buf);
    if (len >= 2 && buf[len - 2] == '\r' && buf[len - 1] == '\n') {
        buf[len - 2] = '\n';
        buf[--len] = '\0';
    }
    char tmp[3 * len + 1];
    size_t n = utf8_sanitize(buf, len, tmp);
    if (n == len && memcmp(buf, tmp, len) == 0)
        return buf;             /* valid */
    if (n >= (size_t)size) {
        n = (size_t)size - 1;
        while (n > 0 && utf8_is_continuation(tmp[n]))
            --n;
    }
    memcpy(buf, tmp, n);
    buf[n] = '\0';
    return buf;
}

/*
 * return pointer to next non-blank char
 */
char *(skipspace)(const char *p)
{
    while (*p == ' ' || *p == '\t')
        ++p;
    return (char *)p;
}

/*
 * find the first valid (non-escaped)
 * char in a string
 */
char *(first_valid)(const char *p, char ch)
{
    if (*p != '\0' && *p != ch) {
        ++p;
        if (ch == ESC2 || ch == ESC) {
            while (*p != '\0' && *p != ch)
                ++p;
        } else {
            while (*p != '\0' && (*p != ch || p[-1] == ESC))
                ++p;
        }
    }
    return (char *)p;
}

/*
 * find the first regular (non-escaped, non in "" () or {} )
 * char in a string
 */
char *(first_regular)(const char *p, char c)
{
    bool quotes = false;
    int paren = 0, braces = 0;

    while (*p != '\0' && (*p != c || quotes || paren > 0 || braces > 0)) {
        bool escaped = false;
        if (*p == ESC) {
            while (*p == ESC)
                ++p;
            escaped = true;
        }
        if (*p == ESC2) {
            while (*p == ESC2)
                ++p;
            escaped = false;
        }
        if (*p == '\0')
            break;
        if (!escaped) {
            if (quotes) {
                if (*p == '\"')
                    quotes = false;
            } else if (*p == '\"') {
                quotes = true;
            } else if (*p == ')') {
                --paren;
            } else if (*p == '(') {
                ++paren;
            } else if (*p == '}') {
                --braces;
            } else if (*p == '{') {
                ++braces;
            }
        }
        ++p;
    }
    return (char *)p;
}

/*
 * the length of the escape \u{XXXX} or \N{name} at 's', after its
 * backslash, of at most 'len' bytes, setting its character in 'cp'; 0 if
 * there is none
 */
static int char_escape_len(const char *s, int len, uint32_t *cp)
{
    if (len < 4 || (s[0] != 'u' && s[0] != 'N') || s[1] != '{')
        return 0;
    const char *end = memchr(s + 2, '}', len - 2);
    if (end == NULL || end == s + 2)
        return 0;
    size_t n = end - (s + 2);
    if (s[0] == 'u') {
        for (size_t i = 0; i < n; ++i) {
            if (!isxdigit((unsigned char)s[2 + i]))
                return 0;
        }
    }
    uint32_t cps[CHARNAME_MAX];
    if (charname_lookup(namesfile, s + 2, n, cps) != 1)
        return 0;
    *cp = cps[0];
    return end + 1 - s;
}

/*
 * remove escapes (backslashes) from a string
 */
int memunescape(char *p, int lenp, bool raw)
{
    if (p == NULL || *p == '\0')
        return 0;

    char *start = p;
    char *s = p;
    enum { NORM, ESCSINGLE, ESCAPE } state = NORM;

    while (lenp != 0) {
        switch (state) {
        case NORM:
            if (*s != ESC) {
                *p++ = *s++;
                --lenp;
                break;
            }
            state = ESCSINGLE;
            ++s;
            if (--lenp == 0)
                break;
            /* fallthrough */
        case ESCSINGLE:
        case ESCAPE:
            if (*s == ESC) {
                state = ESCAPE;
                *p++ = *s++;
                --lenp;
            } else if (*s == ESC2) {
                state = NORM;
                *p++ = ESC;
                ++s;
                --lenp;
            } else {
                uint32_t cp;
                int n = state == ESCSINGLE ? char_escape_len(s, lenp, &cp) : 0;
                if (n > 0) {
                    /* a character by code point or name, never longer in
                     * UTF-8 than its escape */
                    p += utf8_put(cp, p);
                    s += n;
                    lenp -= n;
                } else if (state == ESCSINGLE && lenp >= 3
                           && is_odigit(s[0]) && is_odigit(s[1])
                           && is_odigit(s[2])) {
                    int c = (((s[0] - '0') << 6) | ((s[1] - '0') << 3)
                             | (s[2] - '0'));
                    /* \ooo is an ISO 8859-1 character, unless raw; either
                     * way, the output does not overtake the input */
                    if (raw)
                        *p++ = c;
                    else
                        p += utf8_put(c & 0xff, p);
                    s += 3;
                    lenp -= 3;
                } else {
                    *p++ = *s++;
                    --lenp;
                }
                state = NORM;
            }
            break;
        default:
            break;
        }
    }
    *p = '\0';
    return (int)(p - start);
}

/*
 * remove the spaces at the end of 's', such as after a word completed with
 * Tab; return 's'
 */
char *strip_end(char *s)
{
    size_t len = strlen(s);
    while (len > 0 && s[len - 1] == ' ')
        s[--len] = '\0';
    return s;
}

/* if set, \ooo escapes produce raw bytes (for #rawsend and #rawprint) */
bool raw_escapes = false;

void unescape(char *s)
{
    memunescape(s, strlen(s), raw_escapes);
}

void ptrunescape(ptr *p)
{
    if (p == NULL)
        return;
    p->len = memunescape(ptrdata(p), ptrlen(p), raw_escapes);
}

/* the attributes of the input line and of the messages, for #hilite */
struct attr input_attr, message_attr;
char msgattrbeg[ATTR_SGR_MAX], msgattrend[ATTR_SGR_MAX];

/*
 * print a message of powwow, with the attribute of #hilite messages
 * around each of its lines; not in a signal handler, as it allocates
 */
static int vmessage(const char *format, va_list ap)
{
    va_list ap2;
    va_copy(ap2, ap);
    int len = vsnprintf(NULL, 0, format, ap);
    if (len < 0) {
        va_end(ap2);
        return len;
    }
    char *text = malloc(len + 1);
    if (text == NULL) {
        va_end(ap2);
        out_of_memory();
        return -1;
    }
    vsnprintf(text, len + 1, format, ap2);
    va_end(ap2);

    if (msgattrbeg[0] == '\0') {
        tty_puts(text);
    } else {
        for (char *line = text; *line != '\0'; ) {
            char *nl = strchr(line, '\n');
            if (nl != NULL)
                *nl = '\0';
            if (*line != '\0')
                tty_printf("%s%s%s", msgattrbeg, line, msgattrend);
            if (nl == NULL)
                break;
            tty_putc('\n');
            line = nl + 1;
        }
    }
    free(text);
    return len;
}

/* print a message, redrawing the input line after it */
int message_printf(const char *format, ...)
{
    status(1);
    va_list ap;
    va_start(ap, format);
    int len = vmessage(format, ap);
    va_end(ap);
    return len;
}

/* print a message where the cursor is */
int tty_message(const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    int len = vmessage(format, ap);
    va_end(ap);
    return len;
}

/* the width of the 'len' bytes of 'text', without escape sequences */
static int text_width(const char *text, size_t len)
{
    int width = 0;
    size_t i = 0;
    while (i < len) {
        if (text[i] == ASCII_ESC && i + 1 < len && text[i + 1] == '[') {
            i += 2;
            while (i < len && !is_csi_final((unsigned char)text[i]))
                ++i;
            ++i;
            continue;
        }
        size_t start = i;
        while (i < len && text[i] != ASCII_ESC)
            ++i;
        width += utf8_width(text + start, i - start);
        if (i == start)
            ++i;                /* a lone escape */
    }
    return width;
}

/*
 * the columns that powwow wraps its own text to: those of the terminal,
 * but no more than on one of WRAP_COLUMNS
 */
int wrap_width(void)
{
    return cols_1 < WRAP_COLUMNS - 1 ? cols_1 : WRAP_COLUMNS - 1;
}

/*
 * 'text' wrapped to wrap_width() before the words that do not fit, which
 * go on a new line after 'cont'; NULL if out of memory
 */
static char *wrap_text(const char *text, const char *cont)
{
    int max = wrap_width();
    size_t cont_len = strlen(cont);
    size_t size = strlen(text) + 1 + (strlen(text) / 2 + 1) * (cont_len + 1);
    char *out = malloc(size);
    if (out == NULL)
        return NULL;
    size_t n = 0;
    int col = 0, line_start = 0;
    for (const char *p = text; *p != '\0'; ) {
        if (*p == '\n') {
            out[n++] = *p++;
            col = 0;
            line_start = 0;
            continue;
        }
        /* the spaces before a word, then the word */
        size_t spaces = strspn(p, " ");
        const char *word = p + spaces;
        size_t len = strcspn(word, " \n");
        int width = text_width(word, len);
        if (len > 0 && col > line_start
            && col + (int)spaces + width > max) {
            out[n++] = '\n';
            memcpy(out + n, cont, cont_len);
            n += cont_len;
            line_start = text_width(cont, cont_len);
            col = line_start;
        } else {
            memcpy(out + n, p, spaces);
            n += spaces;
            col += (int)spaces;
        }
        memcpy(out + n, word, len);
        n += len;
        col += width;
        p = word + len;
    }
    out[n] = '\0';
    return out;
}

static int vmessage_wrap(const char *cont, const char *format, va_list ap)
{
    va_list ap2;
    va_copy(ap2, ap);
    int len = vsnprintf(NULL, 0, format, ap);
    char *text = len >= 0 ? malloc(len + 1) : NULL;
    if (text != NULL)
        vsnprintf(text, len + 1, format, ap2);
    va_end(ap2);
    char *out = text != NULL ? wrap_text(text, cont) : NULL;
    free(text);
    if (out == NULL) {
        if (len >= 0)
            out_of_memory();
        return -1;
    }
    len = tty_message("%s\n", out);
    free(out);
    return len;
}

int message_wrap(const char *cont, const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    int len = vmessage_wrap(cont, format, ap);
    va_end(ap);
    return len;
}

int wrap_printf(const char *format, ...)
{
    status(1);
    va_list ap;
    va_start(ap, format);
    int len = vmessage_wrap(WRAP_CONT, format, ap);
    va_end(ap);
    return len;
}

/*
 * add escapes (backslashes) to src
 */
ptr *ptrmescape(ptr *dst, const char *src, int srclen, bool append)
{
    if (src == NULL || srclen <= 0) {
        if (!append)
            ptrzero(dst);
        return dst;
    }

    int len = dst != NULL && append ? ptrlen(dst) : 0;

    dst = ptrsetlen(dst, len + srclen * 4); /* worst case */
    if (MEM_ERROR)
        return dst;

    dst->len = len;
    char *p = ptrdata(dst) + len;

    while (srclen != 0) {
        enum { NORM, ESCAPE } state = NORM;
        if (*src == ESC) {
            while (srclen != 0 && *src == ESC) {
                ++dst->len;
                *p++ = *src++;
                --srclen;
            }

            if (srclen == 0 || *src == ESC2) {
                ++dst->len;
                *p++ = ESC2;
            } else {
                state = ESCAPE;
            }
        }
        if (srclen == 0)
            break;

        /* write control characters as \ooo, but non-ASCII UTF-8 as
         * is: \ooo reads back as an ISO 8859-1 character */
        if ((byte)*src < ' ' || *src == ASCII_DEL) {
            sprintf(p, "\\%03hho", *src++);
            int n = strlen(p);
            dst->len += n;
            p += n;
            --srclen;
        } else {
            if (state == ESCAPE || strchr(SPECIAL_CHARS, *src) != NULL) {
                ++dst->len;
                *p++ = ESC;
            }

            ++dst->len;
            *p++ = *src++;
            --srclen;
        }
    }
    *p = '\0';
    return dst;
}

ptr *ptrescape(ptr *dst, ptr *src, bool append)
{
    if (src == NULL) {
        if (!append)
            ptrzero(dst);
        return dst;
    }
    return ptrmescape(dst, ptrdata(src), ptrlen(src), append);
}

/*
 * add escapes to protect special characters from being escaped.
 */
void escape_specials(char *dst, const char *src)
{
    while (*src != '\0') {
        enum { NORM, ESCAPE } state = NORM;
        if (*src == ESC) {
            while (*src == ESC)
                *dst++ = *src++;

            if (*src == '\0' || *src == ESC2)
                *dst++ = ESC2;
            else
                state = ESCAPE;
        }
        if (*src == '\0')
            break;

        if (*src < ' ' || *src > '~') {
            sprintf(dst, "\\%03hho", *src++);
            dst += strlen(dst);
        } else {
            if (state == ESCAPE || strchr(SPECIAL_CHARS, *src) != NULL)
                *dst++ = ESC;

            *dst++ = *src++;
        }
    }
    *dst = '\0';
}

/*
 * match mark containing & and $ and return true if matched, false if not
 * if true, start and end contain the match bounds
 * if false, start and end are undefined on return
 */
static bool match_mark_or_subst(basenode *bp, const char *src)
{
    char *pat = bp->pattern;
    int mbeg = bp->mbeg;

    /* shortcut for #marks without wildcards */
    if (!bp->wild) {
        if (mbeg != 0 && strncmp(src, pat, strlen(pat)) != 0)
            return false;
        const char *found = strstr(src, pat);
        if (found != NULL) {
            bp->start = found;
            bp->end = found + strlen(pat);
            return true;
        }
        return false;
    }

    bp->start = NULL;

    if (ISMARKWILDCARD(*pat))
        mbeg = -mbeg - 1;  /* pattern starts with '&' or '$' */

    static char mpat[BUFSIZE];
    const char *nsrc = NULL, *prm = NULL, *endprm = NULL;
    bool mword = false;
    while (pat != NULL && *pat != '\0') {
        char c = *pat;
        if (ISMARKWILDCARD(c)) {
            /* & matches a string */
            /* $ matches a single word */
            prm = src;
            if (c == '$')
                mword = true;
            else if (bp->start == NULL)
                bp->start = src;
            ++pat;
        }

        char *npat = first_valid(pat, '&');
        char *npat2 = first_valid(pat, '$');
        if (npat2 < npat)
            npat = npat2;
        if (*npat == '\0')
            npat = NULL;

        if (npat != NULL) {
            my_strncpy(mpat, pat, npat - pat);
            /* mpat[npat - pat] = 0; */
        } else {
            strcpy(mpat, pat);
        }

        if (*mpat != '\0') {
            nsrc = strstr(src, mpat);
            if (nsrc == NULL)
                return false;
            if (mbeg > 0) {
                if (nsrc != src)
                    return false;
                mbeg = 0;  /* reset mbeg to stop further start match */
            }
            endprm = nsrc;
            if (bp->start == NULL) {
                if (prm != NULL)
                    bp->start = src;
                else
                    bp->start = nsrc;
            }
            bp->end = nsrc + strlen(mpat);
        } else if (prm != NULL) {           /* end of pattern space */
            endprm = prm + strlen(prm);
            bp->end = endprm;
        } else {
            bp->end = src;
        }

        /* post-processing of param */
        if (mword) {
            if (prm == NULL)
                return false;
            if (mbeg == -1) {
                int p;
                if (*pat == '\0') {
                    /* the pattern is "$", take first word */
                    p = -1;
                } else {
                    /* unanchored '$' start, take last word */
                    const char *tmp = memrchrs(prm, endprm - prm - 1,
                                               DELIM, DELIM_LEN);
                    if (tmp != NULL)
                        p = tmp - prm;
                    else
                        p = -1;
                }
                prm += p + 1;
                bp->start = prm;
            } else if (*pat == '\0') {
                /* '$' at end of pattern, take first word */
                const char *tmp = memchrs(prm, strlen(prm), DELIM, DELIM_LEN);
                if (tmp != NULL) {
                    endprm = tmp;
                    bp->end = tmp;
                }
                if (bp->start == NULL)
                    bp->start = prm;
            } else {
                /* match only if param is single-worded */
                if (memchrs(prm, endprm - prm, DELIM, DELIM_LEN) != NULL)
                    return false;
            }
        }
        if (prm != NULL) {
            /* reset match flags */
            mbeg = 0;
            mword = false;
        }
        if (nsrc != NULL)
            src = nsrc + strlen(mpat);
        pat = npat;
    }
    return true;
}

/*
 * add marks to line. write in dst.
 */
static ptr *ptrmaddmarks(ptr *dst, const char *line, int len)
{
    ptrzero(dst);

    if (line == NULL || len <= 0)
        return dst;

    for (marknode *mp = markers; mp != NULL; mp = mp->next)
        mp->b.start = NULL;

    const char *lineend = line + len;
    bool start = true;
    marknode *mfirst;
    do {
        mfirst = NULL;
        for (marknode *mp = markers; mp != NULL; mp = mp->next) {
            bool matched;
            if (mp->b.start != NULL && mp->b.start >= line) {
                matched = true;
            } else {
                matched = ((!mp->b.mbeg || start)
                           && match_mark_or_subst(&mp->b, line));
                if (!matched)
                    mp->b.start = lineend;
            }
            if (matched && mp->b.start < lineend
                && (mfirst == NULL || mp->b.start < mfirst->b.start))
                mfirst = mp;
        }

        if (mfirst != NULL) {
            start = false;
            char begin[ATTR_SGR_MAX], end[ATTR_SGR_MAX];
            attr_string(&mfirst->attr, begin, end);

            assert(mfirst->b.start != NULL);
            int matchlen = mfirst->b.start - line;
            dst = ptrmcat(dst, line, matchlen);
            if (MEM_ERROR)
                break;
            line += matchlen;
            len -= matchlen;

            dst = ptrmcat(dst, begin, strlen(begin));
            if (MEM_ERROR)
                break;

            matchlen = mfirst->b.end - mfirst->b.start;
            dst = ptrmcat(dst, line, matchlen);
            if (MEM_ERROR)
                break;
            line += matchlen;
            len -= matchlen;

            dst = ptrmcat(dst, end, strlen(end));
            if (MEM_ERROR)
                break;
        }
    } while (mfirst != NULL);

    if (!MEM_ERROR)
        dst = ptrmcat(dst, line, len);

    return dst;
}

/*
 * add substitutions to line. write in dst.
 */
static ptr *ptrmaddsubst(ptr *dst, const char *line, int len)
{
    ptrzero(dst);

    if (line == NULL || len <= 0)
        return dst;

    for (substnode *sp = substitutions; sp != NULL; sp = sp->next)
        sp->b.start = NULL;

    const char *lineend = line + len;
    bool start = true;

    for (;;) {
        substnode *sfirst = NULL;
        for (substnode *sp = substitutions; sp != NULL; sp = sp->next) {
            bool matched = sp->b.start != NULL && sp->b.start >= line;
            if (!matched) {
                matched = ((!sp->b.mbeg || start)
                           && match_mark_or_subst(&sp->b, line));
                if (!matched)
                    sp->b.start = lineend;
            }
            /* an empty match would never advance through the line */
            if (matched && sp->b.start < lineend && sp->b.end > sp->b.start
                && (sfirst == NULL || sp->b.start < sfirst->b.start))
                sfirst = sp;
        }

        if (sfirst == NULL)
            break;
        start = false;

        assert(sfirst->b.start != NULL);
        int matchlen = sfirst->b.start - line;
        dst = ptrmcat(dst, line, matchlen);
        if (MEM_ERROR)
            break;
        line += matchlen;
        len -= matchlen;

        dst = ptrmcat(dst, sfirst->replacement, strlen(sfirst->replacement));
        if (MEM_ERROR)
            break;
        matchlen = sfirst->b.end - sfirst->b.start;
        line += matchlen;
        len -= matchlen;
    }

    if (!MEM_ERROR)
        dst = ptrmcat(dst, line, len);

    return dst;
}

/*
 * add substitutions and markers to line. write in dst.
 */
ptr *ptrmaddsubst_and_marks(ptr *dst, const char *line, int len)
{
    if (substitutions == NULL)
        return ptrmaddmarks(dst, line, len);
    if (markers == NULL)
        return ptrmaddsubst(dst, line, len);

    static ptr *ptrbuf = NULL;
    ptrbuf = ptrmaddsubst(ptrbuf, line, len);
    if (MEM_ERROR || ptrbuf == NULL) {
        ptrzero(dst);
        return dst;
    }
    return ptrmaddmarks(dst, ptrdata(ptrbuf), ptrlen(ptrbuf));
}

/*
 * write a substitution in dst as the argument of #substitute that defines it
 */
ptr *ptrsubstdef(ptr *dst, bool mbeg, char *pattern, char *replacement)
{
    if (mbeg)
        dst = ptrmcpy(dst, "^", 1);
    else if (*pattern == '^') /* so it does not read back as an anchor */
        dst = ptrmcpy(dst, "\\", 1);
    else
        ptrzero(dst);
    if (MEM_ERROR)
        return dst;

    dst = ptrmescape(dst, pattern, strlen(pattern), true);
    if (MEM_ERROR)
        return dst;
    dst = ptrmcat(dst, "=", 1);
    if (MEM_ERROR)
        return dst;
    return ptrmescape(dst, replacement, strlen(replacement), true);
}

ptr *ptraddsubst_and_marks(ptr *dst, ptr *line)
{
    if (line != NULL)
        return ptrmaddsubst_and_marks(dst, ptrdata(line), ptrlen(line));
    ptrzero(dst);
    return dst;
}

/*
 * Graphic rendition (colors etc.) of the text from the MUD:
 * sgr_stream is in effect after the last text printed for good, and
 * sgr_prompt where the current prompt starts.  Drawing a prompt, which
 * may be erased and redrawn later, may change the terminal's rendition;
 * then sgr_dirty is set, and the rendition is restored before printing
 * more text.
 */
static struct sgr_state sgr_stream, sgr_prompt;
static bool sgr_dirty;

/*
 * end a line from the MUD that has escape sequences and is wider than
 * the screen: reset the rendition, and blank the rest of the row, which
 * the terminal may have filled with the background of the text when it
 * wrapped or scrolled
 */
static void sgr_reset_to_eol(void)
{
    tty_printf("%s%s", tty_modenorm, tty_clreoln);
    sgr_dirty = true;
}

/*
 * write string to tty, word wrapping to next line if needed.
 * don't print a final \n
 */
static void wrap_print(char *s)
{
    char buf[BUFSIZE]; /* ASSERT(cols<BUFSIZE) */
    const char *end = s + strlen(s);

    /* l = left, m = current offset */
    int l = printstrlen(s);
    bool reset = l > cols_1 && l < (int)strlen(s);
    bool follow = true;

    while (l >= cols_1 - col0 && *s != '\0') {
        char *p = buf;
        int m = 0;
        enum { NORM, ESCAPE, BRACKET } state = NORM;
        /* ls = last space in *s, lp = last space in *p */
        char *ls = NULL;
        char *lp = NULL;

        /* this scans over the remaining part of the line adding stuff to
         * print to the buffer and tallying the length of displayed
         * characters */
        while (m < cols_1 - col0 && *s != '\0' && *s != '\n') {
            uint32_t cp;
            size_t n = utf8_cell_len(s, end - s);
            utf8_get(s, n, &cp);
            int w = state == NORM ? utf8_cell_width(s, n) : 0;
            /* a wide character that does not fit goes on the next line */
            if (m > 0 && !tty_cell_fits(col0 + m, s, n, w))
                break;
            memcpy(p, s, n);
            p += n;
            s += n;
            switch (state) {
            case NORM:
                if (cp == ' ') {
                    ls = s;
                    lp = p;
                }

                if (cp == ASCII_ESC) {
                    state = ESCAPE;
                } else if (cp == '\b') {
                    if (m > 0) {
                        --m;
                        ++l;
                    }
                } else if (cp == '\r') {
                    ls = NULL;
                    lp = NULL;
                    m = 0;
                } else {
                    m += w;
                    l -= w;
                }

                break;

            case ESCAPE:
                state = (cp == '[') ? BRACKET : NORM;
                break;

            case BRACKET:
                if (is_csi_final(cp))
                    state = NORM;
                break;
            }
        }

        /* Adjust offsets and stuff to last space */
        if (ls != NULL && ls != s) {
            /* move s back to the last space */
            s = ls;
            /* null term buf[] at last space */
            lp[0] = '\0';
        }

        follow = *s != '\0';

        *p = '\0';
        tty_printf("%s%s", buf, follow ? "\n" : "");
        if (follow)
            col0 = 0;
    }

    if (follow)
        tty_puts(s);
    if (reset)
        sgr_reset_to_eol();
}

/* restore the rendition of the text from the MUD, if needed */
void sgr_output_sync(void)
{
    if (!sgr_dirty)
        return;
    tty_puts(tty_modenorm);
    tty_puts(sgr_stream.seq);
    sgr_dirty = false;
}

/* text from the MUD was printed */
void sgr_output(const char *s, size_t len)
{
    sgr_apply(&sgr_stream, s, len);
}

/* a prompt starts at the current position of the text from the MUD */
void sgr_prompt_start(void)
{
    sgr_prompt = sgr_stream;
}

/* the rendition where the prompt starts */
const char *sgr_prompt_base(void)
{
    return sgr_prompt.seq;
}

/* the prompt is about to be drawn: restore the rendition at its start */
void sgr_prompt_draw(void)
{
    tty_puts(tty_modenorm);
    tty_puts(sgr_prompt.seq);
    sgr_dirty = true;
}

/* the prompt stays on screen: the text from the MUD continues after it */
void sgr_prompt_commit(const char *text, size_t len)
{
    sgr_stream = sgr_prompt;
    sgr_apply(&sgr_stream, text, len);
    sgr_dirty = true;
}

/*
 * add substitutions and marks to line and print.
 * if newline, also print a final \n
 */
void smart_print(const char *line, bool newline)
{
    static ptr *ptrbuf = NULL;

    do {
        if (ptrbuf == NULL) {
            ptrbuf = ptrnew(PARAMLEN);
            if (MEM_ERROR)
                break;
        }
        ptrbuf = ptrmaddsubst_and_marks(ptrbuf, line, strlen(line));
        if (MEM_ERROR || ptrbuf == NULL)
            break;

        char *buf = ptrdata(ptrbuf);

        sgr_output_sync();
        sgr_output(buf, ptrlen(ptrbuf));
        if (opt_wrap) {
            wrap_print(buf);
        } else {
            tty_puts(buf);
            int l = printstrlen(buf);
            if (l > cols_1 && l < ptrlen(ptrbuf))
                sgr_reset_to_eol();
        }
    } while (false);

    if (MEM_ERROR) {
        print_error(error);
    } else if (newline) {
        col0 = 0;
        tty_putc('\n');
    }
}

/*
 * copy first word of src into dst, and return pointer to second word of src;
 * words end at a space or CMDSEP, not at the #delim characters, as command
 * arguments such as host names and file names may contain those
 */
char *split_first_word(char *dst, int dstlen, char *src)
{
    static const char seps[] = { ' ', CMDSEP };

    src = skipspace(src);
    if (*src == '\0') {
        *dst = '\0';
        return src;
    }
    int len = strlen(src);

    char *tmp = memchrs(src, len, seps, sizeof(seps));
    if (tmp != NULL) {
        if (dstlen > tmp - src + 1)
            dstlen = tmp - src + 1;
        my_strncpy(dst, src, dstlen - 1);
    } else {
        my_strncpy(dst, src, dstlen - 1);
        tmp = src + len;
    }
    if (*tmp != '\0' && *tmp != CMDSEP)
        ++tmp;
    return tmp;
}

static void sig_pipe_handler(int signum)
{
    tty_puts("\n#broken pipe.\n");
}

static void sig_winch_handler(int signum)
{
    sig_winch_got = 1;
    sig_pending = 1;
}

static void sig_chld_handler(int signum)
{
    sig_chld_got = 1;
    sig_pending = 1;
}

static noreturn void sig_term_handler(int signum)
{
    tty_printf("%s\n#termination signal.\n", edattrend);
    exit_powwow();
}

static void sig_intr_handler(int signum)
{
    if (confirm) {
        tty_printf("%s\n#interrupted.%s\n", edattrend, tty_clreoln);
        exit_powwow();
    }

    status(1);
    message_wrap(WRAP_CONT, "%s\n#interrupted; press %s again to quit.%s",
                 edattrend, tty_intr_key(), tty_clreoln);
    tty_flush(); /* in case we are not in mainlupe */
    confirm = true;
    error = USER_BREAK;

    sig_oneshot(SIGINT, sig_intr_handler);
}

/*
 * suspend ourselves
 */
void suspend_powwow(int signum)
{
    if (can_suspend) {
        sig_permanent(SIGTSTP, SIG_DFL);
        sig_permanent(SIGTERM, SIG_IGN);
        sig_permanent(SIGINT, SIG_IGN);
        tty_puts(edattrend);
        tty_quit();

        if (kill(0, SIGTSTP) < 0) {
            errmsg("cannot suspend powwow");
            return;
        }

        signal_start();
        tty_start();
        tty_sig_winch_bottomhalf();  /* in case size changed meanwhile */
    } else {
        tty_puts("\n#I don't think your shell has job control.\n");
    }
    status(1);
}

/*
 * set a signal handler permanently (like BSD signal())
 */
function_signal sig_permanent(int signum, function_signal sighandler)
{
    struct sigaction act;
    if (sigaction(signum, NULL, &act) != 0)
        return SIG_ERR;
    function_signal oldhandler = act.sa_handler;
    act.sa_handler = sighandler;
#ifdef SA_RESTART
    act.sa_flags = SA_RESTART;
#else
    act.sa_flags = 0;
#endif
    if (sigaction(signum, &act, NULL) != 0)
        return SIG_ERR;
    return oldhandler;
}

/*
 * One-shot only signal. Hope it will work as intended.
 */
#ifdef SA_ONESHOT
function_signal sig_oneshot(int signum, function_signal sighandler)
{
    struct sigaction act;
    if (sigaction(signum, NULL, &act) != 0)
        return SIG_ERR;
    function_signal oldhandler = act.sa_handler;
    act.sa_handler = sighandler;
    act.sa_flags = SA_ONESHOT;
    if (sigaction(signum, &act, NULL) != 0)
        return SIG_ERR;
    return oldhandler;
}
#endif

/*
 * set up our signal handlers
 */
void signal_start(void)
{
    if (sig_permanent(SIGTSTP, SIG_IGN) == SIG_DFL) {
        sig_permanent(SIGTSTP, suspend_powwow);
        can_suspend = true;
    }
    sig_permanent(SIGCHLD,  sig_chld_handler);
    sig_permanent(SIGQUIT,  sig_intr_handler);
    sig_permanent(SIGTERM,  sig_term_handler);
    sig_permanent(SIGPIPE,  sig_pipe_handler);
    sig_permanent(SIGWINCH, sig_winch_handler);
    /*
     * this must not be permanent, as we want
     * to be able to interrupt system calls
     */
    sig_oneshot(SIGINT, sig_intr_handler);
}

void sig_bottomhalf(void)
{
    if (sig_chld_got != 0)
        sig_chld_bottomhalf();
    if (sig_winch_got != 0)
        tty_sig_winch_bottomhalf();

    sig_winch_got = 0;
    sig_chld_got = 0;
    sig_pending = 0;
}

/* start a message on a line of its own */
static void start_message(void)
{
    clear_input_line(opt_compact);
    if (!opt_compact) {
        tty_putc('\n');
        status(1);
    }
}

/*
 * print "#message: reason", where the message is printf()-formatted and
 * the reason comes from errno
 */
void errmsg(const char *format, ...)
{
    int err = errno;
    if (err == 0 && error == NO_MEM_ERROR)
        err = ENOMEM;

    char msg[BUFSIZE];
    va_list ap;
    va_start(ap, format);
    vsnprintf(msg, sizeof(msg), format, ap);
    va_end(ap);

    start_message();
    if (err == 0)
        message_wrap(WRAP_CONT, "#%s", msg);
    else if (err == EINTR)
        message_wrap(WRAP_CONT, "#%s: interrupted", msg);
    else
        message_wrap(WRAP_CONT, "#%s: %s", msg, strerror(err));
    tty_flush();
}

/*
 * print that memory ran out; code that sets error to NO_MEM_ERROR instead
 * leaves the message to print_error()
 */
void out_of_memory(void)
{
    start_message();
    tty_message("#out of memory\n");
    tty_flush();
}

/*
 * print "#fatal error: message: reason", with the reason from errno, and
 * terminate
 */
void syserr(const char *msg)
{
    int err = errno;
    if (msg != NULL && *msg != '\0') {
        clear_input_line(opt_compact);
        if (!opt_compact)
            tty_putc('\n');
        tty_flush();

        if (err == 0)
            fprintf(stderr, "#fatal error: %s\n", msg);
        else
            fprintf(stderr, "#fatal error: %s: %s\n", msg, strerror(err));
    }

    message_wrap(WRAP_CONT, "#settings NOT saved to file.");

    tty_quit();
    exit(1);
}

/*
 * in a child process, report that exec could not run 'what', and end the
 * child with the status that shells use for a command they cannot run,
 * without what exit() and syserr() do for powwow itself, such as
 * resetting the terminal
 */
void exec_failed(const char *what)
{
    fprintf(stderr, "#cannot run %s: %s\n", what, strerror(errno));
    _exit(127);
}

static void load_missing_stuff(int n)
{
    if (n < 1) {
        tty_add_walk_binds();
        tty_message("#default keypad settings loaded\n");
    }
    if (n < 2) {
        tty_add_initial_binds();
        tty_message("#default editing keys settings loaded\n");
    }
    if (n < 5) {
        static const char *const str[] = {
            "compact", "debug", "echo", "info", "keyecho", "speedwalk", "wrap",
            NULL
        };
        for (int i = 0; str[i] != NULL; ++i) {
            char buf[BUFSIZE];
            sprintf(buf, "#%s={#if ($(1)==\"on\") #option +%s; #else"
                    " #if ($(1)==\"off\") #option -%s; #else #option %s}",
                    str[i], str[i], str[i], str[i]);
            parse_alias(buf);
        }
        message_wrap(WRAP_CONT, "#compatibility aliases loaded: #compact, "
                     "#debug, #echo, #info, #keyecho, #speedwalk, #wrap");
    }
    if (n < 6) {
        char buf[BUFSIZE];
        sprintf(buf, "#lines=#setvar lines=$0");
        parse_alias(buf);
        sprintf(buf, "#settimer=#setvar timer=$0");
        parse_alias(buf);
        limit_mem = 1048576;
        message_wrap(WRAP_CONT,
                     "#compatibility aliases loaded: #lines, #settimer");
        message_wrap(WRAP_CONT, "#max text/strings length set to 1048576 "
                     "bytes; use \"#setvar mem\" to change it");
        tty_message("#wait...");
        tty_flush();
        sleep(1);
        tty_puts("ok\n");
    }
    if (n < READLINE_KEYS_SAVEFILEVER)
        tty_add_new_binds(n);
}

/*
 * read definitions from file
 * return > 0 if success, < 0 if fail.
 * NEVER call syserr() from here or powwow will
 * try to save the definition file even if it got
 * a broken or empty one.
 */
int read_settings(void)
{
    if (*deffile == '\0') {
        wrap_printf("#warning: no save-file defined");
        return 0;
    }

    FILE *f = fopen(deffile, "r");
    if (f == NULL) {
        wrap_printf("#error: cannot open file \"%s\": %s", deffile,
                    strerror(errno));
        return 0;
    }

    ptr *ptrbuf = ptrnew(PARAMLEN);
    if (MEM_ERROR) {
        print_error(error);
        fclose(f);
        return 0;
    }
    char *buf = ptrdata(ptrbuf);
    int left = ptrmax(ptrbuf);
    int len = 0;

    opt_info = false;
    int old_nice = a_nice;
    a_nice = 0;

    for (int n = 0; n < MAX_HASH; ++n) {
        while (aliases[n] != NULL)
            delete_aliasnode(&aliases[n]);
    }
    while (actions != NULL)
        delete_actionnode(&actions);
    while (prompts != NULL)
        delete_promptnode(&prompts);
    while (markers != NULL)
        delete_marknode(&markers);
    while (keydefs != NULL)
        delete_keynode(&keydefs);
    while (substitutions != NULL)
        delete_substnode(&substitutions);
    for (int n = 0; n < MAX_HASH; ++n) {
        while (named_vars[VAR_NUM].buckets[n] != NULL)
            delete_varnode(&named_vars[VAR_NUM].buckets[n], VAR_NUM);
        varnode **first = &named_vars[VAR_TEXT].buckets[n];
        while (*first != NULL) {
            if (is_permanent_variable(*first))
                first = &(*first)->next;
            else
                delete_varnode(first, VAR_TEXT);
        }
    }

    for (int n = -GLOBAL_VAR_COUNT; n < 0; ++n) {
        *num_var(n) = 0;
        ptrdel(text_var(n));
    }

    char *line = NULL;          /* current line, as UTF-8 */
    size_t linesize = 0;
    int savefilever = 0;
    bool limit_mem_hit = false;
    while (left > 0 && fgets(buf + len, left + 1, f) != NULL) {
        /* WARNING: accessing private field ->len */
        int n = strlen(buf + len);
        len += n;
        ptrbuf->len = len;
        left -= n;

        /* Clear all \n prefixed with a literal backslash '\\' */
        char *cmd;
        while ((cmd = strstr(buf, "\\\n")) != NULL) {
            cmd[0] = ' ';
            cmd[1] = ' ';
        }

        cmd = strchr(buf, '\n');

        if (cmd == NULL) {
            if (feof(f)) {
                wrap_printf("#error: missing newline at end of file \"%s\"",
                            deffile);
                break;
            }
            /* no newline yet. increase line size and try again */
            ptrbuf = ptrpad(ptrbuf, ptrlen(ptrbuf) >> 1);
            if (MEM_ERROR) {
                limit_mem_hit = true;
                print_error(error);
                break;
            }
            ptrtrunc(ptrbuf, len);
            buf = ptrdata(ptrbuf);
            left = ptrmax(ptrbuf) - len;
            continue;
        }
        /* got a full line */
        *cmd = '\0';
        left += len;
        len = 0;

        /* convert it to UTF-8 */
        size_t llen = cmd - buf;
        if (3 * llen + 1 > linesize) {
            free(line);
            linesize = 3 * llen + 1;
            line = malloc(linesize);
            if (line == NULL) {
                linesize = 0;
                out_of_memory();
                break;
            }
        }
        if (savefilever < UTF8_SAVEFILEVER)
            llen = latin1_to_utf8(buf, llen, line);
        else
            llen = utf8_sanitize(buf, llen, line);
        line[llen] = '\0';

        cmd = skipspace(line);
        if (*cmd == '\0')
            continue;

        error = 0;
        char *p = first_regular(cmd, ' ');
        if (*p != '\0') {
            *p = '\0';
            if (strcmp(cmd, "#savefile-version") == 0) {
                savefilever = atoi(p + 1);
                continue;
            }
            *p = ' ';
        }
        /* connections did not use TLS unless --tls was given (a #host
         * without it) when files were not UTF-8 yet */
        bool saved_default_tls = default_tls;
        if (savefilever < UTF8_SAVEFILEVER)
            default_tls = false;
        parse_user_input(cmd, true);
        default_tls = saved_default_tls;
        if (error != 0) {
            wrap_printf("#error: loading save-file failed");
            break;
        }
    }

    int failed = 1;
    if (error != 0) {
        failed = -1;
    } else if (ferror(f) && !feof(f)) {
        wrap_printf("#error: cannot read file \"%s\": %s", deffile,
                    strerror(errno));
        failed = -1;
    } else if (limit_mem_hit) {
        wrap_printf("#error: cannot load save-file: got a line longer than "
                    "limit");
        failed = -1;
    } else if (savefilever > SAVEFILEVER) {
        wrap_printf("#warning: save-file is from a newer version of powwow");
    } else if (savefilever < UTF8_SAVEFILEVER) {
        wrap_printf("#definition file converted from ISO-8859-1 to UTF-8");
        load_missing_stuff(savefilever);
    } else if (savefilever < SAVEFILEVER) {
        wrap_printf("#warning: save-file is from an older version of powwow");
        load_missing_stuff(savefilever);
    }

    free(line);
    fclose(f);
    a_nice = old_nice;

    if (ptrbuf != NULL)
        ptrdel(&ptrbuf);

    tty_flush();
    return failed;
}

static char tmpname[BUFSIZE];

static void fail_msg(void)
{
    wrap_printf("#error: cannot write to temporary file \"%s\": %s", tmpname,
                strerror(errno));
}

/*
 * save settings in definition file
 * return > 0 if success, < 0 if fail.
 * NEVER call syserr() from here or powwow will
 * enter an infinite loop!
 */
int save_settings(void)
{
    if (REAL_ERROR) {
        message_printf("#will not save after an error!\n");
        return -1;
    }
    error = 0;

    if (*deffile == '\0') {
        wrap_printf("#warning: no save-file defined");
        return -1;
    }

    /*
     * Create a temporary file in the same directory as deffile,
     * and write settings there
     */
    strcpy(tmpname, deffile);
    int dirlen = strlen(tmpname) - 1;
    while (dirlen != 0 && tmpname[dirlen] != '/')
        --dirlen;
    if (dirlen != 0)
        ++dirlen;

    sprintf(tmpname + dirlen, "tmpsav%d%d", getpid(), rand() >> 8);
    FILE *f = fopen(tmpname, "w");
    if (f == NULL) {
        fail_msg();
        return -1;
    }

    int failed = 1;
    ptr *pp = ptrnew(PARAMLEN);
    if (MEM_ERROR)
        failed = -1;

    if (failed > 0)
        failed = fprintf(f, "#savefile-version %d\n", SAVEFILEVER);
    if (failed > 0 && *hostinfo.name != '\0') {
        failed = fprintf(f, "#host %s %d%s%s\n", hostinfo.name, hostinfo.port,
                         tlsopt_str(&hostinfo.tlsopt),
                         charset_flag_str(hostinfo.utf8));
    }

    if (failed > 0) {
        if (delim_mode == DELIM_CUSTOM) {
            pp = ptrmescape(pp, DELIM, strlen(DELIM), false);
            if (MEM_ERROR)
                failed = -1;
        }
        if (failed > 0) {
            failed = fprintf(f, "#delim %s%s\n", delim_name[delim_mode],
                             delim_mode == DELIM_CUSTOM ? ptrdata(pp) : "");
        }
    }

    char attr[ATTR_NAME_MAX];
    if (failed > 0 && !attr_is_none(&input_attr)) {
        attr_format(&input_attr, attr, sizeof(attr));
        failed = fprintf(f, "#hilite input %s\n", attr);
    }
    if (failed > 0 && !attr_is_none(&message_attr)) {
        attr_format(&message_attr, attr, sizeof(attr));
        failed = fprintf(f, "#hilite messages %s\n", attr);
    }

    if (failed > 0)
        failed = fprintf(f, "#groupdelim %s\n", group_delim);

    if (failed > 0 && *initstr != '\0')
        failed = fprintf(f, "#init =%s\n", initstr);

    if (failed > 0 && limit_mem != 0)
        failed = fprintf(f, "#setvar mem=%d\n", limit_mem);

    if (failed > 0) {
        int size = log_getsize();
        if (size != 0)
            failed = fprintf(f, "#setvar buffer=%d\n", size);
    }

    for (aliasnode *alp = sortedaliases; alp != NULL && failed > 0;
         alp = alp->snext) {
        pp = ptrmescape(pp, alp->name, strlen(alp->name), false);
        if (MEM_ERROR) {
            failed = -1;
            break;
        }
        failed = fprintf(f, "#alias %s%s%s=%s\n", ptrdata(pp),
                         alp->group == NULL ? "" : group_delim,
                         alp->group == NULL ? "" : alp->group,
                         alp->subst);
    }

    for (actionnode *acp = actions; acp != NULL && failed > 0;
         acp = acp->next) {
        failed = fprintf(f, "#action %c%c%s%s%s %s=%s\n",
                         action_chars[acp->type], acp->active ? '+' : '-',
                         acp->label,
                         acp->group == NULL ? "" : group_delim,
                         acp->group == NULL ? "" : acp->group,
                         acp->pattern, acp->command);
    }

    for (promptnode *ptp = prompts; ptp != NULL && failed > 0;
         ptp = ptp->next) {
        failed = fprintf(f, "#prompt %c%c%s %s=%s\n",
                         action_chars[ptp->type], ptp->active ? '+' : '-',
                         ptp->label, ptp->pattern, ptp->command);
    }

    for (marknode *mp = markers; mp != NULL && failed > 0; mp = mp->next) {
        pp = ptrmescape(pp, mp->b.pattern, strlen(mp->b.pattern), false);
        if (MEM_ERROR) {
            failed = -1;
            break;
        }
        failed = fprintf(f, "#mark %s%s=%s\n", mp->b.mbeg ? "^" : "",
                         ptrdata(pp), attr_name(&mp->attr));
    }

    for (substnode *sp = substitutions; sp != NULL && failed > 0;
         sp = sp->next) {
        pp = ptrsubstdef(pp, sp->b.mbeg, sp->b.pattern, sp->replacement);
        if (MEM_ERROR) {
            failed = -1;
            break;
        }
        failed = fprintf(f, "#substitute %s\n", ptrdata(pp));
    }
    /* save value of global variables */

    bool flag = false;
    for (int n = -GLOBAL_VAR_COUNT; n < 0 && failed > 0; ++n) {
        if (*num_var(n) != 0) {
            failed = fprintf(f, "%s@%d = %ld", flag ? ", " : "#(",
                             n, *num_var(n));
            flag = true;
        }
    }
    if (failed > 0 && flag)
        failed = fprintf(f, ")\n");

    for (int n = -GLOBAL_VAR_COUNT; n < 0 && failed > 0; ++n) {
        if (*text_var(n) != NULL && ptrlen(*text_var(n)) != 0) {
            pp = ptrescape(pp, *text_var(n), false);
            if (MEM_ERROR) {
                failed = -1;
                break;
            }
            failed = fprintf(f, "#($%d = \"%s\")\n", n, ptrdata(pp));
        }
    }

    flag = false;
    for (varnode *vp = named_vars[VAR_NUM].sorted; vp != NULL && failed > 0;
         vp = vp->snext) {
        if (vp->num != 0) {
            failed = fprintf(f, "%s@%s = %ld", flag ? ", " : "#(",
                             vp->name, vp->num);
            flag = true;
        }
    }
    if (failed > 0 && flag)
        failed = fprintf(f, ")\n");

    for (varnode *vp = named_vars[VAR_TEXT].sorted; vp != NULL && failed > 0;
         vp = vp->snext) {
        if (!is_permanent_variable(vp) && vp->str != NULL
            && ptrlen(vp->str) != 0) {
            pp = ptrescape(pp, vp->str, false);
            if (MEM_ERROR) {
                failed = -1;
                break;
            }
            failed = fprintf(f, "#($%s = \"%s\")\n", vp->name, ptrdata(pp));
        }
    }

    if (failed > 0 && opt_history) {
        int l = (curline + 1) % MAX_HIST;
        while (failed > 0 && l != curline) {
            if (hist[l] != NULL && *hist[l] != '\0') {
                pp = ptrmescape(pp, hist[l], strlen(hist[l]), false);
                if (MEM_ERROR) {
                    failed = -1;
                    break;
                }
                failed = fprintf(f, "#put %s\n", ptrdata(pp));
            }
            ++l;
            if (l >= MAX_HIST)
                l = 0;
        }
    }

    if (failed > 0 && opt_words) {
        int cl = 4;
        int l = wordindex;
        flag = false;
        while (words[l = words[l].next].word != NULL)
            ;
        while (words[l = words[l].prev].word != NULL && failed > 0) {
            pp = ptrmescape(pp, words[l].word, strlen(words[l].word), false);
            int len = ptrlen(pp) + 1;
            if (cl > 4 && cl + len >= 80) {
                cl = 4;
                failed = fprintf(f, "\n");
                flag = false;
            }
            if (failed > 0)
                failed = fprintf(f, "%s %s", flag ? "" : "#add", ptrdata(pp));
            cl += len;
            flag = true;
        }
        if (failed > 0 && flag)
            failed = fprintf(f, "\n");
    }

    for (keynode *kp = keydefs; kp != NULL && failed > 0; kp = kp->next) {
        if (kp->funct == key_run_command) {
            failed = fprintf(f, "#bind %s %s=%s\n", kp->name,
                             seq_name(kp->sequence, kp->seqlen),
                             kp->call_data);
        } else {
            const char *fname
                = internal_functions[lookup_edit_function(kp->funct)].name;
            failed = fprintf(f, "#bind %s %s=%s%s%s\n", kp->name,
                             seq_name(kp->sequence, kp->seqlen), fname,
                             kp->call_data != NULL ? " " : "",
                             kp->call_data != NULL ? kp->call_data : "");
        }
    }

    if (failed > 0)
        failed = print_all_options(f);

    fclose(f);

    if (error != 0) {
        print_error(error);
    } else if (failed <= 0) {
        fail_msg();
    } else {
        failed = rename(tmpname, deffile);
        if (failed == -1) {
            wrap_printf("#error: cannot move temporary file \"%s\" to "
                        "\"%s\": %s", tmpname, deffile, strerror(errno));
        } else {
            failed = 1;
        }
    }

    if (pp != NULL)
        ptrdel(&pp);

    return failed > 0 ? 1 : -1;
}

/*
 * update "now" to current time
 */
void update_now(void)
{
    if (!now_updated) {
        gettimeofday(&now, NULL);
        now_updated = true;
    }
}

/*
 * terminate powwow as cleanly as possible
 */
void exit_powwow(void)
{
    log_flush();
    if (capturefile != NULL)
        fclose(capturefile);
    if (recordfile != NULL)
        fclose(recordfile);
    if (moviefile != NULL)
        fclose(moviefile);
    save_settings();
    tty_quit();
    exit(0);
}
