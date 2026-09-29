/*
 *  tcp.c  --  telnet protocol communication module for powwow
 *
 *  Copyright (C) 1998,2002 by Massimiliano Ghilardi
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 */

#include <alloca.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/telnet.h>

#include "defines.h"

#ifndef TELOPT_NAWS
#  define TELOPT_NAWS 31
#endif
#ifndef TELOPT_CHARSET
#  define TELOPT_CHARSET 42
#endif
#include <arpa/inet.h>
#ifndef NEXT
#  include <unistd.h>
#endif

#ifdef TERM
#  include "client.h"
#endif

#include "main.h"
#include "utils.h"
#include "tcp.h"
#include "tls.h"
#include "tty.h"
#include "edit.h"
#include "beam.h"
#include "log.h"

#ifdef TELOPTS
# define TELOPTSTR(n) ((n) > NTELOPTS ? "unknown" : telopts[n])
#endif

connsess *main_connection;
connsess *current_connection;

int tcp_max_fd = 0;		   /* highest used fd */

int tcp_count = 0;		   /* number of open connections */
int tcp_attachcount = 0;	   /* number of spawned commands */

int conn_max_index;		   /* 1 + highest used conn_list[] index */

connsess conn_list[MAX_CONNECTS];    /* connection list */

fd_set fdset;			/* set of descriptors to select() on */

enum {
    CHARSET_REQUEST = 1,
    CHARSET_ACCEPTED = 2,
    CHARSET_REJECTED = 3,
    CHARSET_TTABLE_IS = 4,
    CHARSET_TTABLE_REJECTED = 5,
    CHARSET_TTABLE_ACK = 6,
    CHARSET_TTABLE_NAK = 7
};

static void charset_request_subopt(connsess *conn, const char *str,
                                   size_t len)
{
    /* skip optional "[TTABLE]" and version; we never use translation
     * tables, but the server may offer them */
    static const char ttable[] = "[TTABLE]";
    if (len > sizeof ttable - 1
        && memcmp(str, ttable, sizeof ttable - 1) == 0) {
        str += sizeof ttable; /* includes the version octet */
        len -= sizeof ttable;
    }

    if (len == 0)
        return;
    char sep = *str++;
    --len;

    /* conn->cs_flags & CS_REQ_PENDING: both sides sent REQUEST; the
     * server will reject ours, so we answer this one as usual */
    conn->cs_flags &= ~CS_REQ_PENDING;

    /* take the server's first known charset, unless it also offers ours */
    const int want = conn->prefer_utf8 ? CS_UTF8 : CS_LATIN1;
    const char *chosen = NULL;
    size_t chosen_len = 0;
    int chosen_cs = -1;
    for (const char *start = str, *end = str + len; start < end; ) {
        const char *next = memchr(start, sep, end - start);
        if (next == NULL)
            next = end;
        int cs = charset_lookup(start, next - start);
        if (cs >= 0
            && (chosen == NULL || (cs == want && chosen_cs != want))) {
            chosen = start;
            chosen_len = next - start;
            chosen_cs = cs;
        }
        start = next + 1;
    }

    if (chosen == NULL) {
        static const char reject[] = {
            IAC, SB, TELOPT_CHARSET, CHARSET_REJECTED, IAC, SE
        };
        tcp_raw_write(conn, reject, sizeof reject);
        return;
    }

    static const char head[] = {
        IAC, SB, TELOPT_CHARSET, CHARSET_ACCEPTED
    };
    tcp_raw_write(conn, head, sizeof head);
    /* echo the server's spelling of the name */
    tcp_write_escape_iac(conn, chosen, chosen_len);
    static const char tail[] = { IAC, SE };
    tcp_raw_write(conn, tail, sizeof tail);

    tcp_set_charset(conn, chosen_cs, "negotiated");
}

/* the answer to our own CHARSET REQUEST */
static void charset_reply_subopt(connsess *conn, byte type,
                                 const char *str, size_t len)
{
    if (!(conn->cs_flags & CS_REQ_PENDING))
        return;                 /* e.g., rejected after a collision */
    conn->cs_flags &= ~CS_REQ_PENDING;

    switch (type) {
    case CHARSET_ACCEPTED: {
        int cs = charset_lookup(str, len);
        if (cs < 0)
            PRINTF("#warning: server accepted unknown charset \"%.*s\"\n",
                   (int)len, str);
        else
            tcp_set_charset(conn, cs, "negotiated");
        break;
    }
    case CHARSET_REJECTED:
        break;
    default:
        PRINTF("#warning: unexpected CHARSET TTABLE message\n");
        break;
    }
}

/*
 * RFC 2066: a REQUEST may be sent by the side that sent WILL CHARSET
 * and received DO CHARSET.  We only send one when the server has also
 * offered WILL CHARSET, and at most once per connection.
 */
static void maybe_send_charset_request(connsess *conn)
{
    const unsigned char need = CS_SRV_WILL | CS_WILL_SENT | CS_DO_RCVD;
    if ((conn->cs_flags & need) != need || (conn->cs_flags & CS_REQ_SENT))
        return;

    static const char head[] = {
        IAC, SB, TELOPT_CHARSET, CHARSET_REQUEST
    };
    static const char utf8_first[] = ";UTF-8;ISO-8859-1;US-ASCII";
    static const char latin1_first[] = ";ISO-8859-1;UTF-8;US-ASCII";
    static const char tail[] = { IAC, SE };

    tcp_raw_write(conn, head, sizeof head);
    if (conn->prefer_utf8)
        tcp_raw_write(conn, utf8_first, sizeof utf8_first - 1);
    else
        tcp_raw_write(conn, latin1_first, sizeof latin1_first - 1);
    tcp_raw_write(conn, tail, sizeof tail);
    conn->cs_flags |= CS_REQ_SENT | CS_REQ_PENDING;
#ifdef TELOPTS
    tty_printf("[sent SB CHARSET REQUEST %s]\n",
               conn->prefer_utf8 ? utf8_first : latin1_first);
#endif
}

/*
 * change the charset used on a connection
 */
void tcp_set_charset(connsess *conn, enum charset cs, const char *why)
{
    /* any partial character is lost */
    utf8_decoder_reset(&conn->u8dec);

    if (conn->charset == cs)
        return;
    conn->charset = cs;

    if (opt_info) {
        PRINTF("#charset: %s (%s)\n", charset_name(cs), why);
    }

    /* keep the MPI charset in sync with the connection */
    if ((conn->flags & IDEDITOR)
        && !(conn->flags & IDEDITOR_UTF8) != (cs == CS_LATIN1))
        mpi_identify(conn);
}

/*
 * process suboptions.
 * so far, only terminal type is processed but future extensions are
 * window size, X display location, etc.
 */
static void dosubopt(connsess *conn, byte *str, size_t len)
{
    if (str[0] == TELOPT_TTYPE) {
	if (str[1] == 1) {
	    /* 1 == SEND */
#ifdef TELOPTS
	    tty_printf("[got SB TERMINAL TYPE SEND]\n");
#endif
            static const char head[] = { IAC, SB, TELOPT_TTYPE, TELQUAL_IS };
            tcp_raw_write(conn, head, sizeof head);

            const char *term = getenv("TERM");
	    if (term == NULL)
                term = "unknown";
            size_t tlen = strlen(term);
            if (tlen > 256)
                tlen = 256;     /* cap to something sensible */

            tcp_write_escape_iac(conn, term, tlen);

            static const char tail[] = { IAC, SE };
            tcp_raw_write(conn, tail, sizeof tail);
#ifdef TELOPTS
	    tty_printf("[sent SB TERMINAL TYPE IS %s]\n", term);
#endif
	}
    } else if (str[0] == TELOPT_CHARSET && len >= 2) {
        if (str[1] == CHARSET_REQUEST)
            charset_request_subopt(conn, (char *)str + 2, len - 2);
        else
            charset_reply_subopt(conn, str[1], (char *)str + 2, len - 2);
    }
}

/*
 * send an option negotiation
 * 'what' is one of WILL, WONT, DO, DONT
 */
static void sendopt(byte what, byte opt)
{
    byte buf[] = { IAC, what, opt };
    int i;

    while ((i = current_connection->fns->write(
                current_connection, buf, sizeof buf)) < 0
           && errno == EINTR)
	;
    if (i != sizeof buf) {
	errmsg("write option to socket");
	return;
    }

#ifdef TELOPTS
    tty_printf("[sent %s %s]\n", (what == WILL) ? "WILL" :
	       (what == WONT) ? "WONT" :
	       (what == DO) ? "DO" : (what == DONT) ? "DONT" : "error",
	       TELOPTSTR(opt));
#endif
}

/*
 * connect to remote host
 * Warning: some voodoo code here
 */
static int tcp_connect(const char *addr, int port, const char *protocol)
{
    struct addrinfo *host_info;
    struct addrinfo addrinfo = { 0 };
    struct sockaddr_in6 address = { 0 };
    int err, newtcp_fd;

    status(1);

    /*
     * inet_addr has a strange design: It is documented to return -1 for
     * malformed requests, but it is declared to return unsigned long!
     * Anyway, this works.
     */

#ifndef TERM
    switch (inet_pton(AF_INET6, addr, &address.sin6_addr))
    {
    case -1:
        perror("inet_pton()");
        return -1;
    case 1:
	address.sin6_family = AF_INET6;
        addrinfo.ai_family   = AF_INET6;
        addrinfo.ai_socktype = SOCK_STREAM;
        addrinfo.ai_addr     = (struct sockaddr *)&address;
        addrinfo.ai_addrlen  = sizeof address;
        host_info = &addrinfo;
        break;
    case 0:
	if (opt_info)
	    tty_printf("#looking up %s... ", addr);
	tty_flush();
        addrinfo.ai_family   = AF_INET6;
        addrinfo.ai_socktype = SOCK_STREAM;
        addrinfo.ai_flags    = AI_V4MAPPED;
        int gai = getaddrinfo(addr, NULL, &addrinfo, &host_info);
        if (gai != 0) {
            tty_printf("failed to look up host: %s\n",
                       gai_strerror(gai));
            return -1;
        }
	if (opt_info)
	    tty_puts("found.\n");
        break;
    default:
        abort();
    }

    for (; host_info; host_info = host_info->ai_next) {
        newtcp_fd = socket(host_info->ai_family, host_info->ai_socktype, 0);
        if (newtcp_fd == -1)
            continue;
        if (newtcp_fd >= MAX_FDSCAN) {
            tty_printf("#connect: #error: too many open connections\n");
            close(newtcp_fd);
            return -1;
        }

        tty_printf("#trying %s:%d (%s)... ", addr, port, protocol);

        tty_flush();

        ((struct sockaddr_in6 *)host_info->ai_addr)->sin6_port = htons(port);

        err = connect(newtcp_fd, host_info->ai_addr, host_info->ai_addrlen);

        if (err == -1) { /* CTRL-C pressed, or other errors */
            errmsg("connect to host");
            close(newtcp_fd);
            return -1;
        }
        break;
    }

    if (host_info == NULL) {
        errmsg("failed to connect");
        return -1;
    }

#else /* TERM */

    if ((newtcp_fd = connect_server(0)) < 0) {
	tty_puts("\n#powwow: unable to connect to term server\n");
	return -1;
    } else {
	if (newtcp_fd >= MAX_FDSCAN) {
	    tty_printf("#connect: #error: too many open connections\n");
	    close(newtcp_fd);
	    return -1;
	}
	send_command(newtcp_fd, C_PORT, 0, "%s:%d", addr, port);
	tty_puts("Connected to term server...\n");
#ifdef TERM_COMPRESS
	send_command(newtcp_fd, C_COMPRESS, 1, "y");
#endif
	send_command(newtcp_fd, C_DUMB, 1, 0);
    }

#endif /* TERM */

    tty_puts("connected!\n");

    return newtcp_fd;
}

static void tcp_set_fdmode(int fd)
{
#ifndef SOL_TCP
#  define SOL_TCP IPPROTO_TCP
#endif

    /*
     * Now set some options on newtcp_fd :
     * First, no-nagle
     */
    int opt = 1;
    if (setsockopt(fd, SOL_TCP, TCP_NODELAY, &opt, sizeof(opt)))
        errmsg("setsockopt(TCP_NODELAY) failed");

    /* TCP keep-alive */
    if (setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &opt, sizeof(opt)))
        errmsg("setsockopt(SO_KEEPALIVE) failed");

    /*
     * Then, close-on-exec:
     * we don't want children to inherit the socket!
     */

    fcntl(fd, F_SETFD, FD_CLOEXEC);
}



/*
 * we don't expect IAC commands here, except IAC IAC (a protected ASCII 255)
 * which we replace with a single IAC (a plain ASCII 255)
 */
int tcp_unIAC(char *buffer, int len)
{
    char *s, *start, warnIACs = 1;
    if (!memchr(buffer, IAC, len))
	return len;

    for (s = start = buffer; len > 0; buffer++, len--) {
	if (buffer[0] == (char)(byte)IAC) {
	    if (len > 1 && buffer[1] == (char)(byte)IAC)
		buffer++, len--;
	    else if (warnIACs) {
	        PRINTF("#warning: received IAC inside MPI message, treating as IAC IAC.\n");
		warnIACs = 0;
	    }
	}
	*s++ = *buffer;
    }
    return s - start;
}

/*
 * store data byte 'c' from the remote host at 'p', converting it from
 * the connection's charset to UTF-8; returns the new 'p'
 */
static byte *put_data(connsess *conn, byte *p, byte c)
{
    return p + charset_decode_byte(conn->charset, &conn->u8dec, c, (char *)p);
}

/* flush any partial UTF-8 character */
static byte *flush_data(connsess *conn, byte *p)
{
    return p + charset_decode_flush(&conn->u8dec, (char *)p);
}

/*
 * read from remote host using the telnet protocol, and store at most
 * maxsize bytes of UTF-8 text in buffer. return bytes stored.
 */
int tcp_read(connsess *conn, void *buffer, size_t maxsize)
{
    char state = conn->state;
    char old_state = conn->old_state;
    int i;
    static byte subopt[MAX_SUBOPT];
    static int subchars;
    byte *p, *s, *linestart;
    static byte rawbuf[BUFSIZE];

    /*
     * each byte read may become up to three bytes of UTF-8 (U+FFFD);
     * leave room for a pending \r and a pending partial character
     */
    assert(maxsize >= TCP_READ_MIN);
    maxsize = (maxsize - 1 - CHARSET_DECODE_MAX) / TCP_READ_EXPANSION;
    if (maxsize > sizeof rawbuf)
        maxsize = sizeof rawbuf;

    while ((i = conn->fns->read(conn, rawbuf, maxsize)) < 0
           && errno == EINTR)
	;

    if (i == 0 || (i < 0 && errno == EINVAL)) {
	conn->state = NORMAL;
	tcp_close(NULL);
	return 0;
    }
    if (i < 0) {
	errmsg("read from socket");
	return 0;
    }

    /*
     * scan through the buffer,
     * interpret telnet protocol escapes and MUME MPI messages
     */
    p = linestart = (byte *)buffer;
    for (s = rawbuf; i; s++, i--) {
	switch (state) {
	 case NORMAL:
	 case ALTNORMAL:
	 case GOT_R:
	 case GOT_N:
	    /*
	     * Some servers like to send NULs and other funny chars.
	     * Clean up as much as possible.
	     */
	    switch (*s) {
	     case IAC:
                old_state = state; /* remember state to return to after IAC processing */
		state = GOTIAC;
		break;
	     case '\r':
		p = flush_data(conn, p);
		/* start counting \r, unless just got \n */
		if (state == NORMAL || state == ALTNORMAL) {
		    /*
		     * ALTNORMAL is safe here: \r cannot be in MPI header,
		     * and any previous MPI header has already been rejected
		     */
		    state = GOT_R;
		} else if (state == GOT_N)
		  /* after \n\r, we forbid MPI messages */
		    state = ALTNORMAL;
		break;
	     case '\n':
		p = flush_data(conn, p);
		state = GOT_N;
		*p++ = *s;
		linestart = p;
		break;
	     case '\0':
		/* skip NULs */
		p = flush_data(conn, p);
		break;
	     default:
		/* first, flush any missing \r */
		if (state == GOT_R)
		  *p++ = '\r';

		p = put_data(conn, p, *s);

		/* check for MUME MPI messages: */
		if (p - linestart == MPILEN && !memcmp(linestart, MPI, MPILEN)) {
		    if (!(conn->flags & IDEDITOR)) {
			PRINTF("#warning: MPI message received without #request editor!\n");
		    } else if (state == ALTNORMAL) {
			/* no MPI messages after \n\r */
			PRINTF("#warning: MPI attack?\n");
		    } else {
			subchars = process_message((char*)s+1, i-1);
			/* no +MPILEN here, as it was already processed. */
			s += subchars; i-= subchars;
			p = linestart;
		    }
		}

		if (state != ALTNORMAL)
		    state = NORMAL;
		break;
	    }
	    break;

	 case GOTIAC:
	    switch (*s) {
	     case WILL:
		state = GOTWILL; break;
	     case WONT:
		state = GOTWONT; break;
	     case DO:
		state = GOTDO; break;
	     case DONT:
		state = GOTDONT; break;
	     case SB:		/* BUG (multiple connections):	*/
		state = GOTSB;	/* there is only one subopt buffer */
		subchars = 0;
		break;
	     case IAC:
		p = put_data(conn, p, IAC);
		state = old_state;
		break;
	     case GA:
		/* I should handle GA as end-of-prompt marker one day */
		/* one day has come ;) - Max */
		p = flush_data(conn, p);
		prompt_set_iac((char*)p);
		state = old_state;
		break;
	     default:
		/* ignore the rest of the telnet commands */
#ifdef TELOPTS
		tty_printf("[skipped IAC <%d>]\n", *s);
#endif
		state = old_state;
		break;
	    }
	    break;

	 case GOTWILL:
#ifdef TELOPTS
	    tty_printf("[got WILL %s]\n", TELOPTSTR(*s));
#endif
	    switch(*s) {
	     case TELOPT_ECHO:
		/* host echoes, turn off echo here
		 * but only for main connection, since we do not want
		 * subsidiary connection password entries to block anything
		 * in the main connection
		 */
		if (conn == main_connection)
		    linemode |= LM_NOECHO;
		sendopt(DO, *s);
		break;
	     case TELOPT_SGA:
		/* this can't hurt */
		linemode |= LM_CHAR;
		tty_special_keys();
		sendopt(DO, *s);
		break;
	     case TELOPT_CHARSET:
		/* let the server send REQUESTs, and offer to send our own */
		if (!(conn->cs_flags & CS_SRV_WILL))
		    sendopt(DO, *s);
		conn->cs_flags |= CS_SRV_WILL;
		if (!(conn->cs_flags & CS_WILL_SENT)) {
		    sendopt(WILL, *s);
		    conn->cs_flags |= CS_WILL_SENT;
		}
		maybe_send_charset_request(conn);
		break;
	     default:
		/* don't accept other options */
		sendopt(DONT, *s);
		break;
	    }
	    state = old_state;
	    break;

	 case GOTWONT:
#ifdef TELOPTS
	    tty_printf("[got WONT %s]\n", TELOPTSTR(*s));
#endif
	    if (*s == TELOPT_ECHO) {
		/* host no longer echoes, we do it instead */
		linemode &= ~LM_NOECHO;
	    }
	    if (*s == TELOPT_CHARSET) {
		/* only acknowledge if it was enabled */
		if (conn->cs_flags & CS_SRV_WILL)
		    sendopt(DONT, *s);
		conn->cs_flags &= ~CS_SRV_WILL;
		state = old_state;
		break;
	    }
	    /* accept any WONT */
	    sendopt(DONT, *s);
	    state = old_state;
	    break;

	 case GOTDO:
#ifdef TELOPTS
	    tty_printf("[got DO %s]\n", TELOPTSTR(*s));
#endif
	    switch(*s) {
	     case TELOPT_SGA:
		linemode |= LM_CHAR;
		tty_special_keys();
		/* FALLTHROUGH */
	     case TELOPT_TTYPE:
		sendopt(WILL, *s);
		break;
	     case TELOPT_NAWS:
		sendopt(WILL, *s);
		tcp_write_tty_size();
		break;
            case TELOPT_CHARSET:
                if (!(conn->cs_flags & CS_WILL_SENT))
                    sendopt(WILL, *s);
                conn->cs_flags |= CS_WILL_SENT | CS_DO_RCVD;
                maybe_send_charset_request(conn);
                break;
	     default:
		/* accept nothing else */
		sendopt(WONT, *s);
		break;
	    }
	    state = old_state;
	    break;

	 case GOTDONT:
#ifdef TELOPTS
	    tty_printf("[got DONT %s]\n", TELOPTSTR(*s));
#endif
	    if (*s == TELOPT_SGA) {
		linemode &= ~LM_CHAR;
		tty_special_keys();
	    }
	    if (*s == TELOPT_CHARSET) {
		/* only acknowledge if it was enabled; a refusal of our
		 * unsolicited WILL needs no answer */
		if (conn->cs_flags & CS_DO_RCVD)
		    sendopt(WONT, *s);
		conn->cs_flags &= ~(CS_WILL_SENT | CS_DO_RCVD | CS_REQ_PENDING);
		state = old_state;
		break;
	    }
	    sendopt(WONT, *s);
	    state = old_state;
	    break;

	 case GOTSB:
	    if (*s == IAC) {
		state = GOTSBIAC;
	    } else {
		if (subchars < MAX_SUBOPT)
		  subopt[subchars++] = *s;
	    }
	    break;

	 case GOTSBIAC:
	    if (*s == IAC) {
		if (subchars < MAX_SUBOPT)
		  subopt[subchars++] = IAC;
		state = GOTSB;
	    } else if (*s == SE) {
		subopt[subchars] = '\0';
		dosubopt(conn, subopt, subchars);
		state = old_state;
	    } else {
		/* problem! I haven't the foggiest idea of what to do here.
		 * I'll just ignore it and hope it goes away. */
		PRINTF("#telnet: got IAC <%d> instead of IAC SE!\n", (int)*s);
		state = old_state;
	    }
	    break;
	}
    }
    conn->state = state;
    conn->old_state = old_state;

    if (!(current_connection->flags & SPAWN)) {
        log_write(buffer, (char *)p - (char *)buffer, 0, conn->charset);
    }

    return (char *)p - (char *)buffer;
}

static void internal_tcp_raw_write(connsess *conn, const char *data, int len)
{
    while (len > 0) {
        int i;
	while ((i = conn->fns->write(conn, data, len)) < 0
               && errno == EINTR)
	    ;
	if (i < 0) {
	    errmsg("write to socket");
	    break;
	}
	data += i;
	len -= i;
    }
}

void tcp_raw_write(connsess *conn, const void *data, size_t len)
{
    tcp_flush();
    internal_tcp_raw_write(conn, data, len);
}

/* write data, escape any IACs */
void tcp_write_escape_iac(connsess *conn, const void *data, size_t len)
{
    tcp_flush();

    for (;;) {
        const char *iac = memchr(data, IAC, len);
        size_t l = iac ? (iac - (const char *)data) + 1 : len;
        internal_tcp_raw_write(conn, data, l);
        if (iac == NULL)
            return;
        internal_tcp_raw_write(conn, iac, 1);
        len -= l;
        data = iac + 1;
    }
}

/*
 * send characters typed in character mode: convert them to the
 * connection's charset and escape IACs, but add no newline
 */
void tcp_write_chars(connsess *conn, const char *data, size_t len)
{
    char *enc = alloca(len);
    len = charset_encode(conn->charset, data, len, enc);
    tcp_write_escape_iac(conn, enc, len);
}

/*
 * Send current terminal size (RFC 1073)
 */
void tcp_write_tty_size(void)
{
    static const char head[] = { IAC, SB, TELOPT_NAWS };
    tcp_raw_write(current_connection, head, sizeof head);

    char size[] = { cols >> 8, cols & 0xff, lines >> 8, lines & 0xff };
    tcp_write_escape_iac(main_connection, size, sizeof size);

    static const char tail[] = { IAC, SE };
    tcp_raw_write(main_connection, tail, sizeof tail);

#ifdef TELOPTS
    tty_printf("[sent term size %d %d]\n", cols, lines);
#endif
}

/*
 * send a string to the main connection on the remote host
 */
void tcp_main_write(const char *data)
{
    tcp_write(main_connection, data);
}


static char output_buffer[BUFSIZE];
static int output_len = 0;	/* number of characters in output_buffer */
static connsess *output_connection; /* to which socket buffer should be sent*/

static void warn_no_output(void)
{
    PRINTF("#no open connections. Use '#connect main <address> <port>' to"
           " open a connection.\n");
}

/*
 * put data in the output buffer for transmission to the remote host
 */
void tcp_write(connsess *conn, const char *data)
{
    if (conn == NULL) {
        warn_no_output();
        return;
    }

    char *iacs, *out;
    int len, space, iacp;
    len = strlen(data);

    if (main_connection != NULL && main_connection == conn) {
	if (linemode & LM_NOECHO)
	    log_write("", 0, 1, conn->charset); /* log a newline only */
	else
	    log_write(data, len, 1, conn->charset);
	reprint_writeline(data);
    }

    if (conn->charset != CS_UTF8) {
        /* never longer than the original */
        char *enc = alloca(len + 1);
        len = charset_encode(conn->charset, data, len, enc);
        data = enc;
    }

    /* must be AFTER reprint_writeline() */
    if (current_connection->flags & SPAWN)
	status(1);
    else
	status(-1);

    if (conn != output_connection) {
        /* if data to another socket, then flush it */
	tcp_flush();
	output_connection = conn;
    }

    out = output_buffer + output_len;
    space = BUFSIZE - output_len;

    while (len) {
	iacs = memchr(data, IAC, len);
	iacp = iacs ? iacs - data : len;

	if (iacp == 0) {
	    /* we're at the IAC, send it */
	    if (space < 2) {
		tcp_flush();
		out = output_buffer;
		space = BUFSIZE;
	    }
	    *out++ = (char)IAC; *out++ = (char)IAC; output_len += 2; space -= 2;
	    data++; len--;
	    continue;
	}

	while (space < iacp) {
	    memcpy(out, data, space);
	    data += space; len -= space;
	    iacp -= space;
	    output_len = BUFSIZE;

	    tcp_flush();
	    out = output_buffer;
	    space = BUFSIZE;
	}

	if (iacp /* && space >= iacp */ ) {
	    memcpy(out, data, iacp);
	    out += iacp; output_len += iacp; space -= iacp;
	    data += iacp; len -= iacp;
	}
    }
    if (!space) {
	tcp_flush();
	out = output_buffer;
    }
    *out++ = '\n';
    output_len++;
}

/*
 * send all buffered data to the remote host
 */
void tcp_flush(void)
{
    int n;
    char *p = output_buffer;

    if (output_len && output_connection == NULL) {
	clear_input_line(1);
        warn_no_output();
	output_len = 0;
	return;
    }

    if (!output_len)
	return;

    while (output_len) {
	while ((n = output_connection->fns->write(
                    output_connection, p, output_len)) < 0
               && errno == EINTR)
	    ;
	if (n < 0) {
	    output_len = 0;
	    errmsg("write to socket");
	    return;
	}
	sent += n;
	p += n;
	output_len -= n;
    }

    if (output_connection->flags & SPAWN)
	status(1);
    else
	/* sent stuff, so we expect a prompt */
	status(-1);
}

/*
 * Below are multiple-connection support functions:
 */

/*
 * return connection's fd given id,
 * or -1 if null or invalid id is given
 */
connsess *tcp_find_connection(const char *id)
{
    for (int i = 0; i < conn_max_index; i++) {
	if (CONN_INDEX(i).id && !strcmp(CONN_INDEX(i).id, id))
	    return &CONN_INDEX(i);
    }
    return NULL;
}

/*
 * show list of open connections
 */
void tcp_show(void)
{
    int i = tcp_count + tcp_attachcount;

    PRINTF("#%s connection%s opened%c\n", i ? "The following" : "No",
	       i==1 ? " is" : "s are", i ? ':' : '.');


    for (i=0; i<conn_max_index; i++)
	if (CONN_INDEX(i).id && !(CONN_INDEX(i).flags & SPAWN)) {
	    tty_printf("MUD %sactive %s ##%s\t (%s %d, %s)\n",
		       CONN_INDEX(i).flags & ACTIVE ? "   " : "non",
                       (&CONN_INDEX(i) == main_connection
                        ? "(default)" : "         "),
		       CONN_INDEX(i).id,
		       CONN_INDEX(i).host, CONN_INDEX(i).port,
		       charset_name(CONN_INDEX(i).charset));
	}
    for (i=0; i<conn_max_index; i++)
	if (CONN_INDEX(i).id && (CONN_INDEX(i).flags & SPAWN)) {
	    tty_printf("CMD %sactive %s ##%s\t (%s, %s)\n",
		       CONN_INDEX(i).flags & ACTIVE ? "   " : "non",
		       (&CONN_INDEX(i) == main_connection
                        ? "(default)" : "         "),
		       CONN_INDEX(i).id, CONN_INDEX(i).host,
		       charset_name(CONN_INDEX(i).charset));
	}
}

/*
 * permanently change main connection
 */
void tcp_set_main(connsess *conn)
{
    main_connection = conn;
    if (linemode & LM_CHAR)
    {
	linemode = 0;
        tty_special_keys();
    }
    else
	linemode = 0;
    status(-1);
    reprint_clear();
}

static ssize_t tcp_conn_read(connsess *conn, void *buf, size_t count)
{
    return read(conn->fd, buf, count);
}

static ssize_t tcp_conn_write(connsess *conn, const void *buf,
                              size_t count)
{
    return write(conn->fd, buf, count);
}

static int tcp_conn_close(connsess *conn)
{
    return close(conn->fd);
}

static bool tcp_conn_can_read(connsess *conn)
{
    return false;
}

static const struct io_fns tcp_io_fns = {
    .read     = tcp_conn_read,
    .write    = tcp_conn_write,
    .close    = tcp_conn_close,
    .can_read = tcp_conn_can_read
};

/*
 * open another connection
 */
void tcp_open(const char *id, char *initstring, const char *host,
              int port, const struct tls_options *tlsopt, bool utf8)
{
    if (tcp_count + tcp_attachcount >= MAX_CONNECTS) {
	PRINTF("#too many open connections.\n");
	return;
    }
    if (tcp_find_connection(id) != NULL) {
	PRINTF("#connection \"%s\" already open.\n", id);
	return;
    }

    /* find a free slot */
    int i;
    for (i = 0; i < MAX_CONNECTS; i++) {
	if (!CONN_INDEX(i).id)
	    break;
    }
    if (i == MAX_CONNECTS) {
	PRINTF("#internal error, connection table full :(\n");
	return;
    }

    char *conn_host = my_strdup(host);
    char *conn_id   = my_strdup(id);
    if (conn_host == NULL || conn_id == NULL) {
        free(conn_host);
	errmsg("malloc");
	return;
    }

    /* dial the number by moving the right index in small circles */
    int newtcp_fd = tcp_connect(host, port, tlsopt->use_tls ? "TLS" : "TCP");
    if (newtcp_fd < 0) {
	free(conn_host);
	free(conn_id);
	return;
    }

    CONN_INDEX(i) = (connsess){
        .host      = conn_host,
        .id        = conn_id,
        .flags     = ACTIVE,
        .state     = NORMAL,
        .old_state = NORMAL,
        .port      = port,
        .fd        = newtcp_fd,
        .fns       = &tcp_io_fns,
        .prefer_utf8 = utf8,
        .charset   = utf8 ? CS_UTF8 : CS_LATIN1
    };

    if (tlsopt->use_tls && !tls_upgrade(&CONN_INDEX(i), tlsopt)) {
        current_connection = &CONN_INDEX(i);
        tcp_close(NULL);
        return;
    }

    if (tcp_max_fd < newtcp_fd)
	tcp_max_fd = newtcp_fd;
    if (conn_max_index <= i)
	conn_max_index = i+1;

    FD_SET(newtcp_fd, &fdset); 		/* add socket to select() set */
    tcp_count++;

    tcp_set_fdmode(newtcp_fd);

    if (opt_info && tcp_count) {
	PRINTF("#default connection is now \"%s\"\n", id);
    }
    current_connection = &CONN_INDEX(i);
    tcp_set_main(current_connection);
    if (opt_sendsize)
	tcp_write_tty_size();

    if (initstring) {
	parse_instruction(initstring, 0, 0, 1);
	history_done = 0;
    }
}

/*
 * close a connection
 */
void tcp_close(const char *id)
{
    status(1);
    tty_puts(edattrend);
    /*
     * because we may be called from get_remote_input()
     * if tcp_read gets an EOF, before edattrend is
     * printed by get_remote_input() itself.
     */

    connsess *conn = current_connection;
    if (id != NULL) {           /* #zap cmd */
        conn = tcp_find_connection(id);
	if (conn == NULL) {
	    tty_printf("#no such connection: \"%s\"\n", id);
	    return;
	}
    }

    conn->fns->close(conn);

    abort_edit_fd(conn);

    tty_printf("#connection on \"%s\" closed.\n", conn->id);

    if (conn == main_connection) { /* main connection closed */
	if (tcp_count == 1) { /* was last connection */
	    if (opt_exit)
		exit_powwow();
	    tty_puts("#no connections left. Type #quit to quit.\n");
            current_connection = main_connection = NULL;
	} else {
	    /* must find another connection and promote it to main */
	    for (int i=0; i<conn_max_index; i++) {
		if (!CONN_INDEX(i).id || &CONN_INDEX(i) == conn
		    || (CONN_INDEX(i).flags & SPAWN))
		    continue;
		tty_printf("#default connection is now \"%s\"\n", CONN_INDEX(i).id);
		main_connection = &CONN_INDEX(i);
		break;
	    }
	    if (conn == main_connection) {
	        tty_printf("#PANIC! internal error in tcp_close()\nQuitting.\n");
		syserr(NULL);
	    }
	}
	tcp_set_main(main_connection);
    }

    if (current_connection == conn) {
        /* no further I/O allowed on sfd, as we just closed it */
	current_connection = NULL;
    }

    FD_CLR(conn->fd, &fdset);
    if (conn->flags & SPAWN)
	tcp_attachcount--;
    else
	tcp_count--;
    conn->flags = 0;
    conn->state = NORMAL;
    conn->old_state = NORMAL;
    conn->port = 0;
    free(conn->host); conn->host = NULL;
    free(conn->id);   conn->id = NULL;
    free(conn->fragment); conn->fragment = NULL;

    /* recalculate conn_max_index */
    int i = conn - conn_list;
    if (i + 1 == conn_max_index) {
	do {
	    i--;
	} while (i >= 0 && !CONN_INDEX(i).id);
	conn_max_index = i+1;
    }

    /* recalculate tcp_max_fd */
    for (i = tcp_max_fd = 0; i<conn_max_index; i++) {
	if (CONN_INDEX(i).id && tcp_max_fd < CONN_INDEX(i).fd)
	    tcp_max_fd = CONN_INDEX(i).fd;
    }
}

/*
 * toggle output display from another connection
 */
void tcp_togglesnoop(const char *id)
{
    connsess *conn = tcp_find_connection(id);
    if (conn != NULL) {
        conn->flags ^= ACTIVE;
	if (opt_info) {
	    PRINTF("#connection %s is now %sactive.\n",
		      conn->id, conn->flags & ACTIVE ? "" : "non");
	}
    } else {
        PRINTF("#no such connection: %s\n", id);
    }
}

void tcp_spawn(const char *id, char *cmd)
{
    int i, childpid, sockets[2];

    if (tcp_find_connection(id) != NULL) {
	PRINTF("#connection \"%s\" already open.\n", id);
	return;
    }
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) < 0) {
	errmsg("create socketpair");
	return;
    }
    unescape(cmd);

    switch (childpid = fork()) {
      case 0:
	/* child */
	close(0); close(1); close(2);
	setsid();
	dup2(sockets[1], 0);
	dup2(sockets[1], 1);
	dup2(sockets[1], 2);
	close(sockets[0]);
	close(sockets[1]);
	execl("/bin/sh", "sh", "-c", cmd, NULL);
	syserr("execl");
	break;
      case -1:
	close(sockets[0]);
	close(sockets[1]);
	errmsg("fork");
	return;
    }
    close(sockets[1]);

    /* Again, we don't want children to inherit sockets */
    fcntl(sockets[0], F_SETFD, FD_CLOEXEC);

    /* now find a free slot */
    for (i = 0; i < MAX_CONNECTS; i++)
	if (!CONN_INDEX(i).id)
	    break;
    if (i == MAX_CONNECTS) {
	PRINTF("#internal error, connection table full :(\n");
	close(sockets[0]);
	return;
    }

    if (!(CONN_INDEX(i).host = my_strdup(cmd))) {
	errmsg("malloc");
	close(sockets[0]);
	return;
    }
    if (!(CONN_INDEX(i).id = my_strdup(id))) {
	errmsg("malloc");
	free(CONN_INDEX(i).host);
	close(sockets[0]);
	return;
    }
    CONN_INDEX(i).flags = ACTIVE | SPAWN;
    CONN_INDEX(i).state = NORMAL;
    CONN_INDEX(i).old_state = NORMAL;
    CONN_INDEX(i).port = 0;
    CONN_INDEX(i).fd = sockets[0];
    CONN_INDEX(i).fns = &tcp_io_fns;
    CONN_INDEX(i).cs_flags = 0;
    CONN_INDEX(i).prefer_utf8 = hostinfo.utf8;
    CONN_INDEX(i).charset = hostinfo.utf8 ? CS_UTF8 : CS_LATIN1;
    utf8_decoder_reset(&CONN_INDEX(i).u8dec);

    FD_SET(sockets[0], &fdset);	       /* add socket to select() set */
    tcp_attachcount++;

    if (tcp_max_fd < sockets[0])
	tcp_max_fd = sockets[0];
    if (conn_max_index <= i)
	conn_max_index = i+1;

    if (opt_info) {
	PRINTF("#successfully spawned \"%s\" with pid %d\n", id, childpid);
    }

    /*
     * when the child exits we also get an EOF on the socket,
     * so no special care is needed by the SIGCHLD handler.
     */
}

