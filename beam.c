/*
 *  beam.c  --  code to beam texts across the TCP connection following a
 *              special protocol
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
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "defines.h"
#include "main.h"
#include "utils.h"
#include "beam.h"
#include "map.h"
#include "tcp.h"
#include "tty.h"
#include "edit.h"
#include "eval.h"

editsess *edit_sess;	/* head of session list */

char edit_start[BUFSIZE]; /* messages to send to host when starting */ 
char edit_end[BUFSIZE];   /* or leaving editing sessions */ 

static void write_message __P1 (char *,s)
{
    clear_input_line(opt_compact);
    if (!opt_compact) {
	tty_putc('\n');
	status(1);
    }
    tty_puts(s);
}

/*
 * Process editing protocol message from buf with len remaining chars.
 * Return number of characters used in the message.
 */
int process_message __P2 (char *,buf, int,len)
{
    int msglen, i, l, used;
    char *text;
    char msg[BUFSIZE];

    status(1);
    
    msglen = atoi(buf + 1);
    for (i = 1; i < len && isdigit(buf[i]); i++)
      ;
    
    if (i < len && buf[i] == '\r') i++;
    if (i >= len || buf[i] != '\n') {
	write_message ("#warning: MPI protocol error\n");
	return 0;
    }
    
    l = len - ++i;
    if (msglen < l) {
	l = msglen;
	used = msglen + i;
    } else
	used = len;
    
    text = (char *)malloc(msglen);
    if (!text) {
	errmsg("malloc");
	return used > len ? len : used;
    }
    
    memmove(text, buf + i, l);
    i = l;
    while(i < msglen) {
	/*
	 * read() might block here, but it should't be long. I should also
	 * process any telnet protocol commands, but I don't care right now.
	 */
	while ((l = read(tcp_fd, text + i, msglen - i)) == -1 && errno == EINTR)
	    ;
	if (l == -1) {
	    errmsg("read message from socket");
	    return used > len ? len : used;
	}
	tty_printf("\rgot %d chars out of %d", i, msglen);
	tty_flush();
	i += l;
    }
    tty_printf("\rread all %d chars.%s\n", msglen, tty_clreoln);
    
    if ((msglen = tcp_unIAC(text, msglen)))
	switch(*buf) {
	  case 'E':
	    message_edit(text, msglen, 0, 0);
	    break;
	  case 'V':
	    message_edit(text, msglen, 1, 0);
	    break;
	  default:
	    sprintf(msg, "#warning: received a funny message (0x%x)\n", *buf);
	    write_message(msg);
	    free(text);
	    break;
	}
    
    return used;
}

/*
 * abort an editing session when
 * the MUD socket it comes from gets closed
 */
void abort_edit_fd __P1 (int,fd)
{
    editsess **sp, *p;

    if (fd < 0)
	return;
    
    for (sp = &edit_sess; *sp; sp = &(*sp)->next) {
	p = *sp;
	if (p->fd != fd)
	    continue;
    
	if (kill(p->pid, SIGKILL) < 0) {       /* Editicide */
	    errmsg("kill editor child");
	    continue;
	}

	PRINTF("#aborted `%s' (%u)\n", p->descr, p->key);
	p->cancel = 1;
    }
}

/*
 * cancel an editing session; does not free anything
 * (the child death signal handler will remove the session from the list)
 */
void cancel_edit __P1 (editsess *,sp)
{
    char buf[BUFSIZE];
    char keystr[16];
    
    if (kill(sp->pid, SIGKILL) < 0) {       /* Editicide */
	errmsg("kill editor child");
	return;
    }
    PRINTF("#killed `%s' (%u)\n", sp->descr, sp->key);
    sprintf(keystr, "C%u\n", sp->key);
    sprintf(buf, "%sE%d\n%s", MPI, (int)strlen(keystr), keystr);
    tcp_write(sp->fd, buf);
    sp->cancel = 1;
}

/*
 * send back edited text to server, or cancel the editing session if the
 * file was not changed.
 */
static void finish_edit __P1 (editsess *,sp)
{
    char *realtext = NULL, *text;
    int fd, txtlen, hdrlen;
    struct stat sbuf;
    char keystr[16], buf[256], hdr[65];
    
    if (sp->fd == -1)
	goto cleanup_file;
    
    fd = open(sp->file, O_RDONLY);
    if (fd == -1) {
	errmsg("open edit file");
	goto cleanup_file;
    }
    if (fstat(fd, &sbuf) == -1) {
	errmsg("fstat edit file");
	goto cleanup_fd;
    }
    
    txtlen = sbuf.st_size;
    
    if (!sp->cancel && (sbuf.st_mtime > sp->ctime || txtlen != sp->oldsize)) {
	/* file was changed by editor: send back result to server */
	
	realtext = (char *)malloc(2*txtlen + 65);
	/* *2 to protect IACs, +1 is for possible LF, +64 for header */
	if (!realtext) {
	    errmsg("malloc");
	    goto cleanup_fd;
	}
	
	text = realtext + 64;
	if ((txtlen = tcp_read_addIAC(fd, text, txtlen)) == -1)
	    goto cleanup_text;
	
	if (txtlen && text[txtlen - 1] != '\n') {
	    /* If the last line isn't LF-terminated, add an LF;
	     * however, empty files must remain empty */
	    text[txtlen] = '\n';
	    txtlen++;
	}
	
	sprintf(keystr, "E%u\n", sp->key);
	
	sprintf(hdr, "%sE%d\n%s", MPI, (int)(txtlen + strlen(keystr)), keystr);
	
	text -= (hdrlen = strlen(hdr));
	memcpy(text, hdr, hdrlen);
	
	/* text[hdrlen + txtlen] = '\0'; */
	tcp_raw_write(sp->fd, text, hdrlen + txtlen);
	
	sprintf(buf, "#completed session %s (%u)\n", sp->descr, sp->key);
	write_message(buf);
    } else {
	/* file wasn't changed, cancel editing session */
	sprintf(keystr, "C%u\n", sp->key);
	sprintf(hdr, "%sE%d\n%s", MPI, (int) strlen(keystr), keystr);
	
	tcp_raw_write(sp->fd, hdr, strlen(hdr));
	
	sprintf(buf, "#cancelled session %s (%u)\n", sp->descr, sp->key);
	write_message(buf);
    }
    
cleanup_text: if (realtext) free(realtext);
cleanup_fd:   close(fd);
cleanup_file: if (unlink(sp->file) < 0)
		errmsg("unlink edit file");
}

/*
 * start an editing session: process the EDIT/VIEW message
 * if view == 1, text will be viewed, else edited
 */
void message_edit __P4 (char *,text, int,msglen, char,view, char,builtin)
{
    char tmpname[BUFSIZE], command_str[BUFSIZE], buf[BUFSIZE];
    char *errdesc = "#warning: protocol error (message_edit, no %s)\n";
    int tmpfd, i, childpid;
    unsigned int key;
    editsess *s;
    char *editor, *descr;
    char *args[4];
    int waitforeditor;
    
    status(1);

    args[0] = "/bin/sh"; args[1] = "-c";
    args[2] = command_str; args[3] = 0;  

    if (view) {
	key = (unsigned int)-1;
	i = 0;
    } else {
	if (text[0] != 'M') {
	    tty_printf(errdesc, "M");
	    free(text);
	    return;
	}
	for (i = 1; i < msglen && isdigit(text[i]); i++)
	    ;
	if (text[i++] != '\n' || i >= msglen) {
	    tty_printf(errdesc, "\\n");
	    free(text);
	    return;
	}
	key = strtoul(text + 1, NULL, 10);
    }
    descr = text + i;
    while (i < msglen && text[i] != '\n') i++;
    if (i >= msglen) {
	tty_printf(errdesc, "desc");
	free(text);
	return;
    }
    text[i++] = '\0';
    
    sprintf(tmpname, "/tmp/powwow.%u.%d%d", key, getpid(), abs(rand()) >> 8);
    if ((tmpfd = open(tmpname, O_WRONLY | O_CREAT, 0600)) < 0) {
	errmsg("create temp edit file");
	free(text);
	return;
    }
    if (write(tmpfd, text + i, msglen - i) < msglen - i) {
	errmsg("write to temp edit file");
	free(text);
	close(tmpfd);
	return;
    }
    close(tmpfd);
    
    s = (editsess*)malloc(sizeof(editsess));
    if (!s) {
	errmsg("malloc");
	return;
    }
    s->ctime = time((time_t*)NULL);
    s->oldsize = msglen - i;
    s->key = key;
    s->fd = (view || builtin) ? -1 : tcp_fd; /* MUME doesn't expect a reply. */
    s->cancel = 0;
    s->descr = my_strdup(descr);
    s->file = my_strdup(tmpname);
    free(text);
    
    /* send a edit_start message (if wanted) */ 
    if ((!edit_sess) && (*edit_start)) {
	error = 0;
	parse_instruction(edit_start, 0, 0, 1);
	history_done = 0;
    }
    
    if (view) {
	if (!(editor = getenv("POWWOWPAGER")) && !(editor = getenv("PAGER")))
	    editor = "more";
    } else {
	if (!(editor = getenv("POWWOWEDITOR")) && !(editor = getenv("EDITOR")))
	    editor = "emacs";
    }
    
    if (editor[0] == '&') {
	waitforeditor = 0;
	editor++;
    } else
	waitforeditor = 1;
    
    if (waitforeditor) {
	tty_quit();
	/* ignore SIGINT since interrupting the child would interrupt us too,
	 if we are in the same tty group */
	sig_permanent(SIGINT, SIG_IGN);
	sig_permanent(SIGCHLD, SIG_DFL);
    }
    
    switch(childpid = fork()) {		/* let's get schizophrenic */
      case 0:
	sprintf(command_str, "%s %s", editor, s->file);
	sprintf(buf, "TITLE=%s", s->descr);
	putenv(buf);
	/*	   setenv("TITLE", s->descr, 1);*/
	execvp((char *)args[0], (char **)args);
	syserr("execve");
	break;
      case -1:
	errmsg("fork");
	free(s->descr);
	free(s->file);
	free(s);
	return;
    }
    s->pid = childpid;
    if (waitforeditor) {
	while ((i = waitpid(childpid, (int*)NULL, 0)) == -1 && errno == EINTR)
	    ;

	signal_start();		/* reset SIGINT and SIGCHLD handlers */
	tty_start();
	
	if (s->fd != -1) {
	    tty_gotoxy(0, lines - 1);
	    tty_putc('\n');
	}
	
	if (i == -1)
	    errmsg("waitpid");
	else
	    finish_edit(s);
	
	free(s->descr);
	free(s->file);
	if (i != -1 && !edit_sess && *edit_end) {
	    error = 0;
	    parse_instruction(edit_end, 0, 0, 1);
	    history_done = 0;
	}
	
	free(s);
	
    } else {
	s->next = edit_sess;
	edit_sess = s;
    }
}

/*
 * Our child has snuffed it. check if it was an editor, and update the
 * session list if that is the case.
 */
void sig_chld_bottomhalf __P0 (void)
{
    int fd, pid, ret;
    editsess **sp, *p;
   
    /* GH: while() instead of just one check */
    while ((pid = waitpid(-1, &ret, WNOHANG)) > 0) {

	/* GH: check for WIFSTOPPED unnecessary since no */
	/* WUNTRACED to waitpid()			 */
	for (sp = &edit_sess; *sp && (*sp)->pid != pid; sp = &(*sp)->next)
	    ;
	if (*sp) {
	    finish_edit(*sp);
	    p = *sp; *sp = p->next;
	    fd = p->fd;
	    free(p->descr);
	    free(p->file);
	    free(p);

	    /* GH: only send message if found matching session */

	    /* send the edit_end message if this is the last editor... */ 
	    if ((!edit_sess) && (*edit_end)) {
		int otcp_fd = tcp_fd;  /* backup current socket fd */
		tcp_fd = fd;
		error = 0;
		parse_instruction(edit_end, 0, 0, 1);
		history_done = 0;
		tcp_fd = otcp_fd;
	    }
	}
    }
}

