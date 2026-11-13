/*
 *  log.c --  code for #movie / #capture backbuffering
 *            and code for reprint-on-prompt
 *
 *  Copyright (C) 1998 by Massimiliano Ghilardi
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 */

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

#include "defines.h"
#include "main.h"
#include "log.h"
#include "tcp.h"
#include "tty.h"
#include "list.h"
#include "utils.h"
#include "secret.h"

vtime movie_last;                   /* time movie_file was last written */
FILE *capturefile = NULL;           /* capture file or NULL */
FILE *moviefile = NULL;             /* movie file or NULL */
FILE *recordfile = NULL;            /* record file or NULL */

static char *datalist;          /* circular string list */
static int datastart = 0;       /* index to first string start */
static int dataend   = 0;       /* index one past last string end */
static int datasize  = 0;       /* size of circular string list */

#define DATALEFT                                                        \
    (datastart > dataend ? datastart - dataend - 2                      \
     : datastart != 0 ? MAX2(datastart - 1, datasize - dataend - 1)     \
     : datasize - dataend - 1)

typedef struct logentry {
    enum linetype kind;
    long msecs;                 /* millisecs to sleep if kind == SLEEP */
    char *line;                 /* pointer to string in "datalist" circular
                                   buffer */
    enum charset cs;            /* charset to write "line" in */
} logentry;

static logentry *loglist;       /* circular (pointer to string) list */

static int logstart = 0;        /* index to first loglist used */
static int logend   = 0;        /* index one past last loglist used */
static int logsize  = 0;        /* size of circular (pointer to string) list */

#define LOGFULL (logend == (logstart != 0 ? logstart - 1 : logsize - 1))

static const char *const names[] = { NULL, "line", "prompt", "sleep" };

/*
 * write UTF-8 text to 'f', encoded as 'cs'
 */
static void log_fwrite(const char *str, size_t len, enum charset cs, FILE *f)
{
    if (cs == CS_UTF8) {
        fwrite(str, 1, len, f);
        return;
    }
    while (len > 0) {
        char buf[1024];
        size_t n = MIN2(len, sizeof buf);
        /* do not split a character */
        while (n < len && n > 0 && utf8_is_continuation(str[n]))
            --n;
        if (n == 0)
            n = MIN2(len, sizeof buf);
        fwrite(buf, 1, charset_encode(cs, str, n, buf), f);
        str += n;
        len -= n;
    }
}

/*
 * flush a single buffer line
 */
static void log_flushline(int i)
{
    const char *line = loglist[i].line;
    if (capturefile != NULL) {
        log_fwrite(line, strlen(line), loglist[i].cs, capturefile);
        if (loglist[i].kind == LINE)
            fputc('\n', capturefile);
    }
    if (moviefile != NULL) {
        if (loglist[i].msecs != 0)
            fprintf(moviefile, "%s %ld\n", names[SLEEP], loglist[i].msecs);
        fprintf(moviefile, "%s ", names[loglist[i].kind]);
        log_fwrite(line, strlen(line), loglist[i].cs, moviefile);
        fputc('\n', moviefile);
    }
}

/*
 * remove the oldest (first) line from the buffer
 */
static void log_clearline(void)
{
    if (logstart == logend)
        return;
    log_flushline(logstart);

    int next = (logstart + 1) % logsize;
    if (next == logend) {
        datastart = 0;
        dataend = 0;
        logstart = 0;
        logend = 0;
    } else {
        datastart = loglist[next].line - datalist;
        logstart = next;
    }
}

/*
 * remove an initial SLEEP from the buffer
 */
void log_clearsleep(void)
{
    if (logstart != logend)
        loglist[logstart].msecs = 0;
}

/*
 * flush the buffer
 */
void log_flush(void)
{
    int i = logstart;
    while (i != logend) {
        log_flushline(i);
        if (++i == logsize)
            i = 0;
    }
    datastart = 0;
    dataend = 0;
    logstart = 0;
    logend = 0;
}

int log_getsize(void)
{
    return datasize;
}

static void log_reset(void)
{
    if (datasize != 0) {
        free(datalist);
        free(loglist);
        loglist = NULL;
        datalist = NULL;
        logsize = 0;
        datasize = 0;
    }
}

void log_resize(long newsize)
{
    if (newsize != 0 && (newsize < 1000 || newsize > INT_MAX)) {
        message_printf("#buffer size must be 0 (zero) or from 1000 to %d\n",
                       INT_MAX);
        return;
    }

    if (newsize == datasize)
        return;

    log_flush();
    log_reset();
    if (newsize != 0) {
        /* the size comes from the user: running out of memory is not
         * fatal */
        datalist = malloc(newsize);
        loglist = malloc(newsize / 16 * sizeof(logentry));
        if (datalist == NULL || loglist == NULL) {
            errmsg("cannot allocate a buffer of %ld bytes", newsize);
            free(datalist);
            free(loglist);
            datalist = NULL;
            loglist = NULL;
            return;
        }

        datasize = newsize;
        logsize = newsize / 16;
    }
    if (opt_info) {
        message_printf("#buffer resized to %ld bytes%s\n", newsize,
                       newsize != 0 ? "" : " (disabled)");
    }
}

/*
 * add a single line to the buffer
 */
static void log_writeline(const char *line, int len, int kind, long msecs,
                          enum charset cs)
{
    if (++len >= datasize) {
        wrap_printf("#line too long, discarded from movie/capture buffer");
        return;
    }
    while (LOGFULL || DATALEFT < len)
        log_clearline();
    /* ok, now we know there IS enough space */

    int dst;
    if (datastart >= dataend /* is == iff loglist is empty */
        || datasize - dataend > len)
        dst = dataend;
    else
        dst = 0;

    loglist[logend].line = datalist + dst;
    memcpy(loglist[logend].line, line, len - 1);
    datalist[dst + len - 1] = '\0';

    loglist[logend].kind = kind;
    loglist[logend].msecs = msecs;
    loglist[logend].cs = cs;

    dataend = dst + len;
    if (dataend == datasize)
        dataend = 0;

    if (++logend == logsize)
        logend = 0;
}

/*
 * write to #capture / #movie buffer
 */
void log_write(const char *str, int len, bool newline, enum charset cs)
{
    if (datasize == 0 && moviefile == NULL && capturefile == NULL)
        return;

    update_now();
    long diff = diff_vtime(&now, &movie_last);
    movie_last = now;

    bool last = false;
    const char *next;
    do {
        int i;
        next = memchr(str, '\n', len);
        if (next != NULL) {
            i = next - str;
            newline = true;
        } else {
            i = len;
            last = true;
        }

        if (datasize != 0) {
            log_writeline(str, i, last && !newline ? PROMPT : LINE, diff, cs);
        } else {
            if (moviefile != NULL) {
                if (diff != 0)
                    fprintf(moviefile, "%s %ld\n", names[SLEEP], diff);
                fprintf(moviefile, "%s ",
                        names[last && !newline ? PROMPT : LINE]);
                log_fwrite(str, i, cs, moviefile);
                fputc('\n', moviefile);
            }
            if (capturefile != NULL) {
                log_fwrite(str, i, cs, capturefile);
                if (newline) {
                    fputc('\n', capturefile);
                    newline = false;
                }
            }
        }
        diff = 0;
        if (next != NULL) {
            len -= next + 1 - str;
            str = next + 1;
        }
    } while (next != NULL && len > 0);
}

/*
 * write a line to the #record file, in the main connection's charset,
 * unless it contains a secret
 */
void log_record_line(const char *line)
{
    if (recordfile == NULL || secret_in(line))
        return;
    log_fwrite(line, strlen(line),
               main_connection != NULL ? main_connection->charset : CS_LATIN1,
               recordfile);
    fputc('\n', recordfile);
}

static char reprintlist[BUFSIZE];       /* circular string list */
static int  reprintstart = 0;           /* index to first string start */
static int  reprintend   = 0;           /* index one past last string end */
static int  reprintsize  = BUFSIZE;     /* size of circular string list */

#define REPRINTLEFT                                                     \
    (reprintstart > reprintend ? reprintstart - reprintend - 1          \
     : reprintstart != 0                                                \
     ? MAX2(reprintstart - 1, reprintsize - reprintend - 1)             \
     : reprintsize - reprintend - 1)

static char *replist[BUFSIZE / 8];      /* circular (pointer to string) list */

static int repstart = 0;                /* index to first replist used */
static int repend   = 0;                /* index one past last replist used */
static int repsize  = BUFSIZE / 8;      /* size of circular (pointer to string)
                                           list */

#define REPFULL (repend == (repstart != 0 ? repstart - 1 : repsize - 1))

/*
 * remove the oldest (first) line from reprintlist
 */
static void reprint_clearline(void)
{
    if (repstart == repend)
        return;

    int next = (repstart + 1) % repsize;
    if (next == repend) {
        reprintstart = 0;
        reprintend = 0;
        repstart = 0;
        repend = 0;
    } else {
        reprintstart = replist[next] - reprintlist;
        repstart = next;
    }
}

void reprint_clear(void)
{
    reprintstart = 0;
    reprintend = 0;
    repstart = 0;
    repend = 0;
}

/*
 * add a single line to the buffer
 */
void reprint_writeline(const char *line)
{
    int len = strlen(line) + 1;

    if (!opt_reprint || (promptlen != 0 && prompt_status != -1)) {
        /*
         * if prompt is valid, we'll never have to reprint, as we
         * _already_ printed the command at the right moment
         */
        return;
    }

    if (len >= reprintsize) {
        wrap_printf("#line too long, discarded from prompt reprint buffer");
        return;
    }
    while (REPFULL || REPRINTLEFT < len)
        reprint_clearline();
    /* ok, now we know there IS enough space */

    int dst;
    if (reprintstart >= reprintend /* is == iff replist is empty */
        || reprintsize - reprintend > len)
        dst = reprintend;
    else
        dst = 0;

    replist[repend] = reprintlist + dst;
    memcpy(replist[repend], line, len - 1);
    reprintlist[dst + len - 1] = '\0';

    reprintend = dst + len;
    if (reprintend == reprintsize)
        reprintend = 0;

    if (++repend == repsize)
        repend = 0;
}

char *reprint_getline(void)
{
    char *line = NULL;
    if (opt_reprint && repend != repstart)
        line = replist[repstart];
    reprint_clearline();
    return line;
}
