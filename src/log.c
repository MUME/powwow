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

#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <limits.h>
#include <stdint.h>
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
FILE *telnetfile = NULL;            /* #capture --telnet file or NULL */
FILE *pcapfile = NULL;              /* #capture --pcap file or NULL */
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

/* the kinds of the entries of the log, as #movie writes them */
enum linetype { LOG_EMPTY = 0, LOG_LINE = 1, LOG_PROMPT = 2, LOG_SLEEP = 3 };

typedef struct logentry {
    enum linetype kind;
    long msecs;                 /* millisecs to sleep if kind == LOG_SLEEP */
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
        if (loglist[i].kind == LOG_LINE)
            fputc('\n', capturefile);
    }
    if (moviefile != NULL) {
        if (loglist[i].msecs != 0)
            fprintf(moviefile, "%s %ld\n", names[LOG_SLEEP], loglist[i].msecs);
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
 * remove an initial LOG_SLEEP from the buffer
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

/* the calls of log_resize(), so that read_settings() knows if a file
 * set the size */
unsigned log_resizes = 0;

void log_resize(long newsize)
{
    ++log_resizes;
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
static void log_writeline(const char *line, int len, enum linetype kind,
                          long msecs, enum charset cs)
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
            log_writeline(str, i, last && !newline ? LOG_PROMPT : LOG_LINE,
                          diff, cs);
        } else {
            if (moviefile != NULL) {
                if (diff != 0)
                    fprintf(moviefile, "%s %ld\n", names[LOG_SLEEP], diff);
                fprintf(moviefile, "%s ",
                        names[last && !newline ? LOG_PROMPT : LOG_LINE]);
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
 * write to the #capture --telnet file the 'len' bytes at 'data', as the
 * MUD sent them
 */
void log_telnet(const void *data, size_t len)
{
    if (telnetfile != NULL && len > 0)
        fwrite(data, 1, len, telnetfile);
}

/* the link type of Wireshark's exported PDUs, which name their dissector */
#define PCAP_LINKTYPE_UPPER_PDU 252
#define PCAP_SNAPLEN            65535

/* the tags of an exported PDU */
enum {
    PDU_TAG_END       = 0,
    PDU_TAG_PROTO     = 12,
    PDU_TAG_IPV4_SRC  = 20,
    PDU_TAG_IPV4_DST  = 21,
    PDU_TAG_IPV6_SRC  = 22,
    PDU_TAG_IPV6_DST  = 23,
    PDU_TAG_PORT_TYPE = 24,
    PDU_TAG_SRC_PORT  = 25,
    PDU_TAG_DST_PORT  = 26
};

static void put_be16(unsigned char *p, unsigned v)
{
    p[0] = v >> 8;
    p[1] = v;
}

static void put_be32(unsigned char *p, uint32_t v)
{
    put_be16(p, v >> 16);
    put_be16(p + 2, v & 0xffff);
}

/* add tag 'tag' with the 'len' bytes at 'data' at 'p'; return its end */
static unsigned char *put_tag(unsigned char *p, unsigned tag,
                              const void *data, unsigned len)
{
    put_be16(p, tag);
    put_be16(p + 2, len);
    memcpy(p + 4, data, len);
    return p + 4 + len;
}

static unsigned char *put_tag32(unsigned char *p, unsigned tag, uint32_t v)
{
    unsigned char buf[4];
    put_be32(buf, v);
    return put_tag(p, tag, buf, sizeof buf);
}

/*
 * start the #capture --pcap file 'f', unless it has data already, as
 * when appending to it
 */
void log_pcap_start(FILE *f)
{
    if (fseek(f, 0, SEEK_END) != 0 || ftell(f) != 0)
        return;
    /* in the byte order of the writer, which the magic number tells */
    const uint32_t magic = 0xa1b2c3d4;
    const uint16_t version[] = { 2, 4 };
    const uint32_t rest[] = {
        0,                      /* time zone */
        0,                      /* time stamp accuracy */
        PCAP_SNAPLEN,
        PCAP_LINKTYPE_UPPER_PDU
    };
    fwrite(&magic, sizeof magic, 1, f);
    fwrite(version, sizeof version, 1, f);
    fwrite(rest, sizeof rest, 1, f);
}

/*
 * add at 'p' the tags of the address and port of 'sa', the source if
 * 'src', or else the destination, unless it is neither IPv4 nor IPv6;
 * return their end
 */
static unsigned char *put_addr_tags(unsigned char *p, bool src,
                                    const struct sockaddr_storage *sa)
{
    unsigned port_tag = src ? PDU_TAG_SRC_PORT : PDU_TAG_DST_PORT;
    if (sa->ss_family == AF_INET) {
        const struct sockaddr_in *in = (const struct sockaddr_in *)sa;
        p = put_tag(p, src ? PDU_TAG_IPV4_SRC : PDU_TAG_IPV4_DST,
                    &in->sin_addr, 4);
        return put_tag32(p, port_tag, ntohs(in->sin_port));
    }
    if (sa->ss_family == AF_INET6) {
        const struct sockaddr_in6 *in6 = (const struct sockaddr_in6 *)sa;
        p = put_tag(p, src ? PDU_TAG_IPV6_SRC : PDU_TAG_IPV6_DST,
                    &in6->sin6_addr, 16);
        return put_tag32(p, port_tag, ntohs(in6->sin6_port));
    }
    return p;
}

/*
 * write to the #capture --pcap file the 'len' bytes at 'data', which
 * powwow sent to the MUD on socket 'fd' if 'to_mud', or else read from
 * it, as telnet packets between the addresses and ports of the socket
 */
void log_pcap(int fd, bool to_mud, const void *data, size_t len)
{
    if (pcapfile == NULL)
        return;

    struct sockaddr_storage mud = { 0 }, user = { 0 };
    socklen_t salen = sizeof mud;
    if (getpeername(fd, (struct sockaddr *)&mud, &salen) != 0)
        mud.ss_family = AF_UNSPEC;
    salen = sizeof user;
    if (getsockname(fd, (struct sockaddr *)&user, &salen) != 0)
        user.ss_family = AF_UNSPEC;

    unsigned char tags[128];
    unsigned char *p = put_tag(tags, PDU_TAG_PROTO, "telnet", 6);
    p = put_tag32(p, PDU_TAG_PORT_TYPE, 2);         /* TCP */
    p = put_addr_tags(p, true, to_mud ? &user : &mud);
    p = put_addr_tags(p, false, to_mud ? &mud : &user);
    put_be16(p, PDU_TAG_END);
    put_be16(p + 2, 0);
    p += 4;
    size_t tagslen = p - tags;

    struct timeval tv;
    gettimeofday(&tv, NULL);
    const char *s = data;
    while (len > 0) {
        size_t n = MIN2(len, PCAP_SNAPLEN - tagslen);
        const uint32_t record[] = {
            tv.tv_sec, tv.tv_usec, tagslen + n, tagslen + n
        };
        fwrite(record, sizeof record, 1, pcapfile);
        fwrite(tags, 1, tagslen, pcapfile);
        fwrite(s, 1, n, pcapfile);
        s += n;
        len -= n;
    }
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
