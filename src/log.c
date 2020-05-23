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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

#include "defines.h"
#include "main.h"
#include "log.h"
#include "tty.h"
#include "list.h"
#include "utils.h"

vtime movie_last;		     /* time movie_file was last written */
FILE *capturefile = (FILE *)NULL;    /* capture file or NULL */
FILE *moviefile = (FILE *)NULL;	     /* movie file or NULL */
FILE *recordfile = (FILE *)NULL;     /* record file or NULL */


static char *datalist;		/* circular string list */
static int datastart = 0;	/* index to first string start */
static int dataend   = 0;	/* index one past last string end */
static int datasize  = 0;	/* size of circular string list */

#define DATALEFT (datastart > dataend ? datastart - dataend - 2 : datastart ? \
		MAX2(datastart - 1, datasize - dataend - 1) : datasize - dataend - 1)

typedef struct logentry {
    enum linetype kind;
    long msecs;			/* millisecs to sleep if kind == SLEEP */
    char *line;			/* pointer to string in "datalist" circular buffer */
} logentry;

logentry *loglist;		/* circular (pointer to string) list */

static int logstart = 0;	/* index to first loglist used */
static int logend   = 0;	/* index one past last loglist used */
static int logsize  = 0;	/* size of circular (pointer to string) list */

#define LOGFULL (logend == (logstart ? logstart - 1 : logsize - 1))

static char *names[] = { NULL, "line", "prompt", "sleep" };

/*
 * flush a single buffer line
 */
static void log_flushline(int i)
{
    if (capturefile)
	fprintf(capturefile, "%s%s",
		loglist[i].line, loglist[i].kind == LINE ? "\n" : "");
    if (moviefile) {
	if (loglist[i].msecs)
	    fprintf(moviefile, "%s %ld\n",
		    names[SLEEP], loglist[i].msecs);
	fprintf(moviefile, "%s %s\n",
		names[loglist[i].kind], loglist[i].line);
    }
}

/*
 * remove the oldest (first) line from the buffer
 */
static void log_clearline(void)
{
    int next;

    if (logstart == logend)
	return;
    log_flushline(logstart);

    next = (logstart + 1) % logsize;
    if (next == logend)
	datastart = dataend = logstart = logend = 0;
    else
	datastart = loglist[next].line - datalist, logstart = next;
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
    datastart = dataend = logstart = logend = 0;
}

int log_getsize(void)
{
    return datasize;
}

static void log_reset(void)
{
    if (datasize) {
	if (datalist) free(datalist);
	if (loglist)  free(loglist);
	loglist = NULL;
	datalist = NULL;
	logsize = datasize = 0;
    }
}


void log_resize(int newsize)
{
    if (newsize && newsize < 1000) {
	PRINTF("#buffer size must be 0 (zero) or >= 1000\n");
	return;
    }

    if (newsize == datasize)
	return;

    log_flush();
    log_reset();
    if (newsize) {
	datalist = (char *)malloc(newsize);
	if (!datalist) { log_reset(); errmsg("malloc"); return; }

	loglist = (logentry *)malloc(newsize/16*sizeof(logentry));
	if (!loglist) { log_reset(); errmsg("malloc"); return; }

	datasize = newsize;
	logsize = newsize / 16;
    }
    if (opt_info) {
	PRINTF("#buffer resized to %d bytes%s\n", newsize, newsize ? "" : " (disabled)");
    }
}

/*
 * add a single line to the buffer
 */
static void log_writeline(char *line, int len, int kind, long msecs)
{
    int dst;

    if (++len >= datasize) {
	PRINTF("#line too long, discarded from movie/capture buffer\n");
	return;
    }
    while (LOGFULL || DATALEFT < len)
	log_clearline();
    /* ok, now we know there IS enough space */

    if (datastart >= dataend /* is == iff loglist is empty */
	|| datasize - dataend > len)
	dst = dataend;
    else
	dst = 0;

    memcpy(loglist[logend].line = datalist + dst, line, len - 1);
    datalist[dst + len - 1] = '\0';

    loglist[logend].kind = kind;
    loglist[logend].msecs = msecs;

    if ((dataend = dst + len) == datasize)
	dataend = 0;

    if (++logend == logsize)
	logend = 0;
}

/*
 * write to #capture / #movie buffer
 */
void log_write(char *str, int len, int newline)
{
    char *next;
    long diff;
    int i, last = 0;

    if (!datasize && !moviefile && !capturefile)
	return;

    update_now();
    diff = diff_vtime(&now, &movie_last);
    movie_last = now;

    do {
	if ((next = memchr(str, '\n', len))) {
	    i = next - str;
            newline = 1;
        } else {
	    i = len;
            last = 1;
	}

	if (datasize)
	    log_writeline(str, i, last && !newline ? PROMPT : LINE, diff);
	else {
	    if (moviefile) {
		if (diff)
		    fprintf(moviefile, "%s %ld\n",
			names[SLEEP], diff);
		fprintf(moviefile, "%s %.*s\n",
			names[last && !newline ? PROMPT : LINE], i, str);
	    }
	    if (capturefile) {
                fwrite(str, 1, i, capturefile);
                if (newline) {
                    const char nl[1] = "\n";
                    fwrite(nl, 1, 1, capturefile);
                    newline = 0;
                }
            }
	}
	diff = 0;
	if (next) {
	    len -= next + 1 - str;
	    str = next + 1;
	}
    } while (next && len > 0);
}

static char reprintlist[BUFSIZE];	/* circular string list */
static int  reprintstart = 0;		/* index to first string start */
static int  reprintend   = 0;		/* index one past last string end */
static int  reprintsize  = BUFSIZE;	/* size of circular string list */

#define REPRINTLEFT (reprintstart > reprintend ? reprintstart - reprintend - 1 : reprintstart ? \
			MAX2(reprintstart - 1, reprintsize - reprintend - 1) : reprintsize - reprintend - 1)

static char *replist[BUFSIZE/8];	/* circular (pointer to string) list */

static int repstart = 0;		/* index to first replist used */
static int repend   = 0;		/* index one past last replist used */
static int repsize  = BUFSIZE/8;	/* size of circular (pointer to string) list */

#define REPFULL (repend == (repstart ? repstart - 1 : repsize - 1))

/*
 * remove the oldest (first) line from reprintlist
 */
static void reprint_clearline(void)
{
    int next;

    if (repstart == repend)
	return;

    next = (repstart + 1) % repsize;
    if (next == repend)
	reprintstart = reprintend = repstart = repend = 0;
    else
	reprintstart = replist[next] - reprintlist, repstart = next;
}

void reprint_clear(void)
{
    reprintstart = reprintend = repstart = repend = 0;
}

/*
 * add a single line to the buffer
 */
void reprint_writeline(char *line)
{
    int len = strlen(line) + 1;
    int dst;

    if (!opt_reprint || (promptlen && prompt_status != -1))
	/*
	 * if prompt is valid, we'll never have to reprint, as we
	 * _already_ printed the command at the right moment
	 */
	return;

    if (len >= reprintsize) {
	PRINTF("#line too long, discarded from prompt reprint buffer\n");
	return;
    }
    while (REPFULL || REPRINTLEFT < len)
	reprint_clearline();
    /* ok, now we know there IS enough space */

    if (reprintstart >= reprintend /* is == iff replist is empty */
	|| reprintsize - reprintend > len)
	dst = reprintend;
    else
	dst = 0;

    memcpy(replist[repend] = reprintlist + dst, line, len - 1);
    reprintlist[dst + len - 1] = '\0';

    if ((reprintend = dst + len) == reprintsize)
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

