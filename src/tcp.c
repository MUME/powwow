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
#ifndef TELOPT_NAWS
#  define TELOPT_NAWS 31
#endif
#include <arpa/inet.h>
#ifndef NEXT
#  include <unistd.h>
#endif

#ifdef TERM
#  include "client.h"
#endif

#include "defines.h"
#include "main.h"
#include "utils.h"
#include "tcp.h"
#include "tty.h"
#include "edit.h"
#include "beam.h"
#include "log.h"

#ifdef TELOPTS
# define TELOPTSTR(n) ((n) > NTELOPTS ? "unknown" : telopts[n])
#endif

int tcp_fd = -1;                   /* current socket file descriptor
				    * -1 means no socket */
int tcp_main_fd = -1;		   /* socket file descriptor of main connect.
				    * -1 means no socket */
int tcp_max_fd = 0;		   /* highest used fd */

int tcp_count = 0;		   /* number of open connections */
int tcp_attachcount = 0;	   /* number of spawned commands */

int conn_max_index;		   /* 1 + highest used conn_list[] index */

connsess conn_list[MAX_CONNECTS];    /* connection list */

byte conn_table[MAX_FDSCAN];	     /* fd -> index translation table */

fd_set fdset;			/* set of descriptors to select() on */

/*
 * process suboptions.
 * so far, only terminal type is processed but future extensions are
 * window size, X display location, etc.
 */
static void dosubopt(byte *str)
{
    char buf[256], *term;
    int len, err;

    if (str[0] == TELOPT_TTYPE) {
	if (str[1] == 1) {
	    /* 1 == SEND */
#ifdef TELOPTS
	    tty_printf("[got SB TERMINAL TYPE SEND]\n");
#endif
	    if (!(term = getenv("TERM"))) term = "unknown";
	    sprintf(buf, "%c%c%c%c%.*s%c%c", IAC, SB, TELOPT_TTYPE, 0,
		    256-7, term, IAC, SE);	/* 0 == IS */

	    len = strlen(term) + 6;
	    while ((err = write(tcp_fd, buf, len)) < 0 && errno == EINTR)
	        ;
	    if (err != len) {
	        errmsg("write subopt to socket");
		return;
	    }
#ifdef TELOPTS
	    tty_printf("[sent SB TERMINAL TYPE IS %s]\n", term);
#endif
	}
    }
}

/*
 * send an option negotiation
 * 'what' is one of WILL, WONT, DO, DONT
 */
static void sendopt(byte what, byte opt)
{
    static byte buf[3] = { IAC, 0, 0 };
    int i;
    buf[1] = what; buf[2] = opt;

    while ((i = write(tcp_fd, buf, 3)) < 0 && errno == EINTR)
	;
    if (i != 3) {
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
int tcp_connect(const char *addr, int port)
{
    struct addrinfo *host_info;
    struct addrinfo addrinfo;
    struct sockaddr_in6 address;
    int err, newtcp_fd;

    status(1);

    memset(&address, 0, sizeof address);
    memset(&addrinfo, 0, sizeof addrinfo);

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

        tty_printf("#trying %s... ", addr);
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

#else /* term */

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

#endif /* term */

    tty_puts("connected!\n");


    {
	/*
	 * Now set some options on newtcp_fd :
	 * First, no-nagle
	 */
	int opt = 1;
#	ifndef SOL_TCP
#	 define SOL_TCP IPPROTO_TCP
#	endif
	if (setsockopt(newtcp_fd, SOL_TCP, TCP_NODELAY, &opt, sizeof(opt)))
	    errmsg("setsockopt(TCP_NODELAY) failed");

	/* TCP keep-alive */
	if (setsockopt(newtcp_fd, SOL_SOCKET, SO_KEEPALIVE, &opt, sizeof(opt)))
	    errmsg("setsockopt(SO_KEEPALIVE) failed");

	/*
	 * Then, close-on-exec:
	 * we don't want children to inherit the socket!
	 */

	fcntl(newtcp_fd, F_SETFD, FD_CLOEXEC);
    }

    return newtcp_fd;
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
 * read a maximum of size chars from remote host
 * using the telnet protocol. return chars read.
 */
int tcp_read(int fd, char *buffer, int maxsize)
{
    char state = CONN_LIST(fd).state;
    char old_state = CONN_LIST(fd).old_state;
    int i;
    static byte subopt[MAX_SUBOPT];
    static int subchars;
    byte *p, *s, *linestart;

    char *ibuffer = buffer;
    if (state == GOT_R) {
        /* make room for the leading \r */
        ++ibuffer;
        --maxsize;
    }

    while ((i = read(fd, ibuffer, maxsize)) < 0 && errno == EINTR)
	;

    if (i == 0) {
	CONN_LIST(fd).state = NORMAL;
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
    p = (byte *)buffer;
    for (s = linestart = (byte *)ibuffer; i; s++, i--) {
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
		state = GOT_N;
		*p++ = *s;
		linestart = p;
		break;
	     case '\0':
		/* skip NULs */
		break;
	     default:
		/* first, flush any missing \r */
		if (state == GOT_R)
		  *p++ = '\r';

		*p++ = *s;

		/* check for MUME MPI messages: */
		if (p - linestart == MPILEN && !memcmp(linestart, MPI, MPILEN)) {
		    if (!(CONN_LIST(fd).flags & IDEDITOR)) {
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
		*p++ = IAC;
		state = old_state;
		break;
	     case GA:
		/* I should handle GA as end-of-prompt marker one day */
		/* one day has come ;) - Max */
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
		if (fd == tcp_main_fd)
		    linemode |= LM_NOECHO;
		sendopt(DO, *s);
		break;
	     case TELOPT_SGA:
		/* this can't hurt */
		linemode |= LM_CHAR;
		tty_special_keys();
		sendopt(DO, *s);
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
		dosubopt(subopt);
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
    CONN_LIST(fd).state = state;
    CONN_LIST(fd).old_state = old_state;

    if (!(CONN_LIST(tcp_fd).flags & SPAWN)) {
        log_write(buffer, (char *)p - buffer, 0);
    }

    return (char *)p - buffer;
}

static void internal_tcp_raw_write(int fd, const char *data, int len)
{
    while (len > 0) {
        int i;
	while ((i = write(fd, data, len)) < 0 && errno == EINTR)
	    ;
	if (i < 0) {
	    errmsg("write to socket");
	    break;
	}
	data += i;
	len -= i;
    }
}

void tcp_raw_write(int fd, const char *data, int len)
{
    tcp_flush();
    internal_tcp_raw_write(fd, data, len);
}

/* write data, escape any IACs */
void tcp_write_escape_iac(int fd, const char *data, int len)
{
    tcp_flush();

    for (;;) {
        const char *iac = memchr(data, IAC, len);
        size_t l = iac ? (iac - data) + 1 : len;
        internal_tcp_raw_write(fd, data, l);
        if (iac == NULL)
            return;
        internal_tcp_raw_write(fd, iac, 1);
        len -= l;
        data = iac + 1;
    }
}

/*
 * Send current terminal size (RFC 1073)
 */
void tcp_write_tty_size(void)
{
    static byte buf[] = { IAC, SB, TELOPT_NAWS, 0, 0, 0, 0, IAC, SE };

    buf[3] = cols >> 8;
    buf[4] = cols & 0xff;
    buf[5] = lines >> 8;
    buf[6] = lines & 0xff;

    tcp_raw_write(tcp_main_fd, (char *)buf, 9);
#ifdef TELOPTS
    tty_printf("[sent term size %d %d]\n", cols, lines);
#endif
}

/*
 * send a string to the main connection on the remote host
 */
void tcp_main_write(char *data)
{
    tcp_write(tcp_main_fd, data);
}


static char output_buffer[BUFSIZE];
static int output_len = 0;	/* number of characters in output_buffer */
static int output_socket = -1;	/* to which socket buffer should be sent*/

/*
 * put data in the output buffer for transmission to the remote host
 */
void tcp_write(int fd, char *data)
{
    char *iacs, *out;
    int len, space, iacp;
    len = strlen(data);

    if (tcp_main_fd != -1 && tcp_main_fd == fd) {
	if (linemode & LM_NOECHO)
	    log_write("", 0, 1); /* log a newline only */
	else
	    log_write(data, len, 1);
	reprint_writeline(data);
    }

    /* must be AFTER reprint_writeline() */
    if (CONN_LIST(tcp_fd).flags & SPAWN)
	status(1);
    else
	status(-1);

    if (fd != output_socket) { /* is there data to another socket? */
	tcp_flush(); /* then flush it */
	output_socket = fd;
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

    if (output_len && output_socket == -1) {
	clear_input_line(1);
	PRINTF("#no open connections. Use '#connect main <address> <port>' to open a connection.\n");
	output_len = 0;
	return;
    }

    if (!output_len)
	return;

    while (output_len) {
	while ((n = write(output_socket, p, output_len)) < 0 && errno == EINTR)
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

    if (CONN_LIST(output_socket).flags & SPAWN)
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
int tcp_find(char *id)
{
    int i;

    for (i=0; i<conn_max_index; i++) {
	if (CONN_INDEX(i).id && !strcmp(CONN_INDEX(i).id, id))
	    return CONN_INDEX(i).fd;
    }
    return -1;
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
	    tty_printf("MUD %sactive %s ##%s\t (%s %d)\n",
		       CONN_INDEX(i).flags & ACTIVE ? "   " : "non",
		       i == tcp_main_fd ? "(default)" : "         ",
		       CONN_INDEX(i).id,
		       CONN_INDEX(i).host, CONN_INDEX(i).port);
	}
    for (i=0; i<conn_max_index; i++)
	if (CONN_INDEX(i).id && (CONN_INDEX(i).flags & SPAWN)) {
	    tty_printf("CMD %sactive %s ##%s\t (%s)\n",
		       CONN_INDEX(i).flags & ACTIVE ? "   " : "non",
		       i == tcp_main_fd ? "(default)" : "         ",
		       CONN_INDEX(i).id, CONN_INDEX(i).host);
	}
}

/*
 * permanently change main connection
 */
void tcp_set_main(int fd)
{
    /* GH: reset linemode and prompt */
    tcp_main_fd = fd;
    if (linemode & LM_CHAR)
	linemode = 0, tty_special_keys();
    else
	linemode = 0;
    status(-1);
    reprint_clear();
}

/*
 * open another connection
 */
void tcp_open(char *id, char *initstring, char *host, int port)
{
    int newtcp_fd, i;

    if (tcp_count+tcp_attachcount >= MAX_CONNECTS) {
	PRINTF("#too many open connections.\n");
	return;
    }
    if (tcp_find(id)>=0) {
	PRINTF("#connection \"%s\" already open.\n", id);
	return;
    }

    /* find a free slot */
    for (i=0; i<MAX_CONNECTS; i++) {
	if (!CONN_INDEX(i).id)
	    break;
    }
    if (i == MAX_CONNECTS) {
	PRINTF("#internal error, connection table full :(\n");
	return;
    }

    if (!(CONN_INDEX(i).host = my_strdup(host))) {
	errmsg("malloc");
	return;
    }
    if (!(CONN_INDEX(i).id = my_strdup(id))) {
	errmsg("malloc");
	free(CONN_INDEX(i).host);
	return;
    }

    /* dial the number by moving the right index in small circles */
    if ((newtcp_fd = tcp_connect(host, port)) < 0) {
	free(CONN_INDEX(i).host);
	free(CONN_INDEX(i).id);
	CONN_INDEX(i).id = 0;
	return;
    }

    conn_table[newtcp_fd] = i;
    CONN_INDEX(i).flags = ACTIVE;
    CONN_INDEX(i).state = NORMAL;
    CONN_INDEX(i).old_state = NORMAL;
    CONN_INDEX(i).port = port;
    CONN_INDEX(i).fd = newtcp_fd;

    if (tcp_max_fd < newtcp_fd)
	tcp_max_fd = newtcp_fd;
    if (conn_max_index <= i)
	conn_max_index = i+1;

    FD_SET(newtcp_fd, &fdset); 		/* add socket to select() set */
    tcp_count++;

    if (opt_info && tcp_count) {
	PRINTF("#default connection is now \"%s\"\n", id);
    }
    tcp_set_main(tcp_fd = newtcp_fd);
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
void tcp_close(char *id)
{
    int i, sfd;

    status(1);
    tty_puts(edattrend);
    /*
     * because we may be called from get_remote_input()
     * if tcp_read gets an EOF, before edattrend is
     * printed by get_remote_input() itself.
     */

    if (id) {  /* #zap cmd */
	if ((sfd = tcp_find(id)) < 0) {
	    tty_printf("#no such connection: \"%s\"\n", id);
	    return;
	}
    } else
	sfd = tcp_fd;  /* connection closed by remote host */

    shutdown(sfd, 2);
    close(sfd);

    abort_edit_fd(sfd);

    tty_printf("#connection on \"%s\" closed.\n", CONN_LIST(sfd).id);

    if (sfd == tcp_main_fd) { /* main connection closed */
	if (tcp_count == 1) { /* was last connection */
	    if (opt_exit)
		exit_powwow();
	    tty_puts("#no connections left. Type #quit to quit.\n");
	    tcp_fd = tcp_main_fd = -1;
	} else {
	    /* must find another connection and promote it to main */
	    for (i=0; i<conn_max_index; i++) {
		if (!CONN_INDEX(i).id || CONN_INDEX(i).fd == sfd
		    || (CONN_INDEX(i).flags & SPAWN))
		    continue;
		tty_printf("#default connection is now \"%s\"\n", CONN_INDEX(i).id);
		tcp_main_fd = CONN_INDEX(i).fd;
		break;
	    }
	    if (sfd == tcp_main_fd) {
	        tty_printf("#PANIC! internal error in tcp_close()\nQuitting.\n");
		syserr(NULL);
	    }
	}
	tcp_set_main(tcp_main_fd);
    }

    if (tcp_fd == sfd)
	tcp_fd = -1; /* no further I/O allowed on sfd, as we just closed it */

    FD_CLR(sfd, &fdset);
    if (CONN_LIST(sfd).flags & SPAWN)
	tcp_attachcount--;
    else
	tcp_count--;
    CONN_LIST(sfd).flags = 0;
    CONN_LIST(sfd).state = NORMAL;
    CONN_LIST(sfd).old_state = NORMAL;
    CONN_LIST(sfd).port = 0;
    free(CONN_LIST(sfd).host); CONN_LIST(sfd).host = 0;
    free(CONN_LIST(sfd).id);   CONN_LIST(sfd).id = 0;
    if (CONN_LIST(sfd).fragment) {
	free(CONN_LIST(sfd).fragment);
	CONN_LIST(sfd).fragment = 0;
    }

    /* recalculate conn_max_index */
    i = conn_table[sfd];
    if (i+1 == conn_max_index) {
	do {
	    i--;
	} while (i>=0 && !CONN_INDEX(i).id);
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
void tcp_togglesnoop(char *id)
{
    int sfd;

    sfd = tcp_find(id);
    if (sfd>=0) {
        CONN_LIST(sfd).flags ^= ACTIVE;
	if (opt_info) {
	    PRINTF("#connection %s is now %sactive.\n",
		      CONN_LIST(sfd).id, CONN_LIST(sfd).flags & ACTIVE ? "" : "non");
	}
    } else {
        PRINTF("#no such connection: %s\n", id);
    }
}

void tcp_spawn(char *id, char *cmd)
{
    int i, childpid, sockets[2];

    if (tcp_find(id)>=0) {
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
    for (i=0; i<MAX_CONNECTS; i++) {
	if (!CONN_INDEX(i).id) {
	    conn_table[sockets[0]] = i;
	    break;
	}
    }
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

