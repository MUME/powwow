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
#include <limits.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <arpa/telnet.h>

#include "defines.h"
#include "main.h"
#include "utils.h"
#include "beam.h"
#include "map.h"
#include "tcp.h"
#include "tty.h"
#include "edit.h"
#include "eval.h"

editsess *edit_sess;            /* head of session list */

char edit_start[BUFSIZE];       /* messages to send to host when starting */
char edit_end[BUFSIZE];         /* or leaving editing sessions */

static void write_message(const char *s)
{
    clear_input_line(opt_compact);
    if (!opt_compact) {
        tty_putc('\n');
        status(1);
    }
    tty_puts(s);
}

/*
 * identify for remote editing; announce UTF-8 support unless the
 * connection uses ISO 8859-1
 */
void mpi_identify(connsess *conn)
{
    static const char latin1[] = MPI "I\n";
    static const char utf8[] = MPI "I1\n1";

    if (conn->charset == CS_LATIN1) {
        tcp_raw_write(conn, latin1, sizeof latin1 - 1);
        conn->ideditor_utf8 = false;
    } else {
        tcp_raw_write(conn, utf8, sizeof utf8 - 1);
        conn->ideditor_utf8 = true;
    }
    conn->ideditor = true;
}

/*
 * Process editing protocol message from buf with len remaining chars.
 * Return number of characters used in the message.
 */
int process_message(connsess *conn, char *buf, int len)
{
    status(1);

    /* the length must be positive and fit in an int */
    int msglen = 0;
    bool bad_len = false;
    int i = 1;
    for (; i < len && isdigit((unsigned char)buf[i]); ++i) {
        int digit = buf[i] - '0';
        if (msglen > (INT_MAX - digit) / 10)
            bad_len = true;
        else
            msglen = msglen * 10 + digit;
    }

    if (i < len && buf[i] == '\r')
        ++i;
    if (i >= len || buf[i] != '\n' || bad_len || msglen == 0) {
        write_message("#warning: bad MPI message header\n");
        return 0;
    }

    ++i;
    int l = len - i;

    char *text = malloc(msglen);
    if (text == NULL) {
        out_of_memory();
        return i;
    }

    /* only doing trivial (IAC IAC) processing; this should all be
     * rewritten */
    bool got_iac = false;
    char *from = buf + i;
    char *to = text;
    for (i = 0; i < l && (to - text) < msglen; ++i) {
        if (got_iac) {
            if (*from != (char)IAC)
                wrap_printf("#warning: unescaped IAC in MPI message, kept"
                            " as is");
            else
                ++from;
            got_iac = false;
        } else {
            got_iac = *from == (char)IAC;
            *to++ = *from++;
        }
    }
    i = to - text;
    int used = from - buf;

    while (i < msglen) {
        /* read() might block here, but it should't be long */
        while ((l = conn->fns->read(conn, text + i, msglen - i)) == -1
               && errno == EINTR)
            ;
        if (l <= 0) {
            /* Cygwin reports the end of a #spawn command as a reset or
             * aborted connection */
            if (l == 0 || errno == ECONNRESET || errno == ECONNABORTED)
                wrap_printf("#connection \"%s\" closed in the middle of"
                            " an MPI message", conn->id);
            else
                errmsg("cannot read from connection \"%s\"", conn->id);
            free(text);
            return used > len ? len : used;
        }
        from = text + i;
        to = text + i;
        for (i = 0; i < l; ++i) {
            if (got_iac) {
                if (*from != (char)IAC)
                    wrap_printf("#warning: unescaped IAC in MPI message,"
                                " kept as is");
                else
                    ++from;
                got_iac = false;
            } else {
                got_iac = *from == (char)IAC;
                *to++ = *from++;
            }
        }
        i = to - text;
        tty_printf("\rgot %d chars out of %d", i, msglen);
        tty_flush();
    }
    tty_printf("\rread all %d chars.%s\n", msglen, tty_clreoln);

    switch (*buf) {
    case 'E':
        message_edit(conn, text, msglen, false, false);
        break;
    case 'V':
        message_edit(conn, text, msglen, true, false);
        break;
    default: {
        char msg[BUFSIZE];
        sprintf(msg, "#warning: unknown MPI message type 0x%02hhx\n", *buf);
        write_message(msg);
        free(text);
        break;
    }
    }

    return used;
}

/*
 * abort an editing session when
 * the MUD socket it comes from gets closed
 */
void abort_edit_fd(connsess *conn)
{
    if (conn == NULL)
        return;

    for (editsess *p = edit_sess; p != NULL; p = p->next) {
        if (p->conn != conn)
            continue;

        /* nothing can be sent to the closed connection */
        p->conn = NULL;
        if (kill(p->pid, SIGKILL) < 0) {        /* Editicide */
            errmsg("cannot stop the editor");
            continue;
        }

        wrap_printf("#aborted \"%s\" (%u)", p->descr, p->key);
        p->cancel = true;
    }
}

/*
 * cancel an editing session; does not free anything
 * (the child death signal handler will remove the session from the list)
 */
void cancel_edit(editsess *sp)
{
    if (kill(sp->pid, SIGKILL) < 0) {           /* Editicide */
        errmsg("cannot stop the editor");
        return;
    }
    wrap_printf("#killed \"%s\" (%u)", sp->descr, sp->key);
    char keystr[16];
    sprintf(keystr, "C%u\n", sp->key);
    char buf[BUFSIZE];
    sprintf(buf, "%sE%zu\n%s", MPI, strlen(keystr), keystr);
    tcp_write(sp->conn, buf);
    sp->cancel = true;
}

static ssize_t read_file(int fd, void *buf, size_t count)
{
    char *p = buf;
    size_t result = 0;
    while (count > 0) {
        int r;
        do {
            r = read(fd, p, count);
        } while (r < 0 && errno == EINTR);
        if (r < 0)
            return -1;
        if (r == 0)
            break;
        result += r;
        p += r;
        count -= r;
    }
    return result;
}

/*
 * send back edited text to server, or cancel the editing session if the
 * file was not changed.
 */
static void finish_edit(editsess *sp)
{
    char *realtext = NULL;

    if (sp->conn == NULL)
        goto cleanup_file;

    int fd = open(sp->file, O_RDONLY);
    if (fd == -1) {
        errmsg("cannot open %s", sp->file);
        goto cleanup_file;
    }
    struct stat sbuf;
    if (fstat(fd, &sbuf) == -1) {
        errmsg("cannot read %s", sp->file);
        goto cleanup_fd;
    }

    int txtlen = sbuf.st_size;

    if (!sp->cancel
        && (sbuf.st_mtime > sp->ctime || txtlen != sp->oldsize)) {
        /* file was changed by editor: send back result to server */

        realtext = malloc(txtlen + 65);
        /* +1 is for possible LF, +64 for header */
        if (realtext == NULL) {
            out_of_memory();
            goto cleanup_fd;
        }

        char *text = realtext + 64;
        txtlen = read_file(fd, text, txtlen);
        if (txtlen < 0)
            goto cleanup_text;

        if (txtlen != 0 && text[txtlen - 1] != '\n') {
            /* If the last line isn't LF-terminated, add an LF;
             * however, empty files must remain empty */
            text[txtlen] = '\n';
            ++txtlen;
        }

        char keystr[16];
        sprintf(keystr, "E%u\n", sp->key);

        char hdr[65];
        sprintf(hdr, "%sE%zu\n%s", MPI, txtlen + strlen(keystr), keystr);

        int hdrlen = strlen(hdr);
        text -= hdrlen;
        memcpy(text, hdr, hdrlen);

        /* text[hdrlen + txtlen] = '\0'; */
        tcp_write_escape_iac(sp->conn, text, hdrlen + txtlen);

        char buf[256];
        sprintf(buf, "#completed session %s (%u)\n", sp->descr, sp->key);
        write_message(buf);
    } else {
        /* file wasn't changed, cancel editing session */
        char keystr[16];
        sprintf(keystr, "C%u\n", sp->key);
        char hdr[65];
        sprintf(hdr, "%sE%zu\n%s", MPI, strlen(keystr), keystr);

        tcp_raw_write(sp->conn, hdr, strlen(hdr));

        char buf[256];
        sprintf(buf, "#cancelled session %s (%u)\n", sp->descr, sp->key);
        write_message(buf);
    }

 cleanup_text:
    free(realtext);
 cleanup_fd:
    close(fd);
 cleanup_file:
    if (unlink(sp->file) < 0)
        errmsg("cannot delete %s", sp->file);
}

/*
 * start an editing session: process the EDIT/VIEW message
 * if view is true, text will be viewed, else edited
 */
void message_edit(connsess *conn, char *text, int msglen, bool view,
                  bool builtin)
{
    const char *errdesc = "#warning: bad MPI edit message\n";

    status(1);

    unsigned int key = ~0U;
    int i = 0;
    if (!view) {
        if (msglen < 1 || text[0] != 'M') {
            tty_puts(errdesc);
            free(text);
            return;
        }
        for (i = 1; i < msglen && isdigit((unsigned char)text[i]); ++i)
            ;
        if (i >= msglen || text[i++] != '\n' || i >= msglen) {
            tty_puts(errdesc);
            free(text);
            return;
        }
        key = strtoul(text + 1, NULL, 10);
    }
    char *descr = text + i;
    while (i < msglen && text[i] != '\n')
        ++i;
    if (i >= msglen) {
        tty_puts(errdesc);
        free(text);
        return;
    }
    text[i++] = '\0';

    const char *tmpdir = getenv("TMPDIR");
    if (tmpdir == NULL || *tmpdir == '\0')
        tmpdir = "/tmp";
    char tmpname[BUFSIZE];
    int len = snprintf(tmpname, sizeof tmpname, "%s/powwow.%u.XXXXXX",
                       tmpdir, key);
    int tmpfd = -1;
    if (len < 0 || len >= (int)sizeof tmpname)
        errno = ENAMETOOLONG;
    else
        tmpfd = mkstemp(tmpname);
    if (tmpfd < 0) {
        errmsg("cannot create %s", tmpname);
        free(text);
        return;
    }
    if (write(tmpfd, text + i, msglen - i) < msglen - i) {
        errmsg("cannot write %s", tmpname);
        free(text);
        close(tmpfd);
        unlink(tmpname);
        return;
    }
    close(tmpfd);

    editsess *s = malloc(sizeof(editsess));
    if (s == NULL) {
        out_of_memory();
        free(text);
        unlink(tmpname);
        return;
    }
    s->ctime = time(NULL);
    s->oldsize = msglen - i;
    s->key = key;
    s->conn = (view || builtin) ? NULL : conn;
    s->cancel = false;
    s->out_fd = -1;
    s->out_len = 0;
    /* the editor gets the title as sent; we keep it as UTF-8 */
    char *title = my_strdup(descr);
    if (builtin) {
        s->descr = my_strdup(descr);
    } else {
        size_t dlen = strlen(descr);
        s->descr = malloc(3 * dlen + 1);
        if (s->descr != NULL) {
            if (conn != NULL && conn->ideditor_utf8)
                dlen = utf8_sanitize(descr, dlen, s->descr);
            else
                dlen = latin1_to_utf8(descr, dlen, s->descr);
            s->descr[dlen] = '\0';
        }
    }
    s->file = my_strdup(tmpname);
    free(text);

    /* send a edit_start message (if wanted) */
    if (edit_sess == NULL && *edit_start != '\0') {
        error = 0;
        parse_instruction(edit_start, false, false, true);
        history_done = 0;
    }

    /* #option pager and editor come first, then the environment */
    const char *editor = view ? opt_pager : opt_editor;
    if (editor == NULL)
        editor = getenv(view ? "POWWOWPAGER" : "POWWOWEDITOR");
    if (editor == NULL)
        editor = getenv(view ? "PAGER" : "EDITOR");
    if (editor == NULL)
        editor = view ? "more" : "vi";

    bool waitforeditor = true;
    if (editor[0] == '&') {
        waitforeditor = false;
        ++editor;
    }

    if (waitforeditor) {
        tty_quit();
        /*
         * ignore SIGINT since interrupting the child would interrupt us
         * too, if we are in the same tty group
         */
        sig_permanent(SIGINT, SIG_IGN);
        sig_permanent(SIGCHLD, SIG_DFL);
    }

    /* a background editor must not write over the screen */
    int out_pipe[2] = { -1, -1 };
    if (!waitforeditor && pipe(out_pipe) < 0) {
        errmsg("cannot capture the output of the editor");
        out_pipe[0] = -1;
        out_pipe[1] = -1;
    }

    int childpid = fork();              /* let's get schizophrenic */
    switch (childpid) {
    case 0: {
        if (out_pipe[1] >= 0) {
            int null_fd = open("/dev/null", O_RDONLY);
            if (null_fd >= 0) {
                dup2(null_fd, 0);
                close(null_fd);
            }
            dup2(out_pipe[1], 1);
            dup2(out_pipe[1], 2);
            close(out_pipe[0]);
            close(out_pipe[1]);
        }
        /* any character of $TMPDIR is safe in "$1" */
        char command_str[BUFSIZE];
        snprintf(command_str, sizeof command_str, "%s \"$1\"", editor);
        setenv("TITLE", title, 1);
        execl("/bin/sh", "/bin/sh", "-c", command_str, "/bin/sh", s->file,
              (char *)NULL);
        exec_failed("the editor");
        break;
    }
    case -1:
        if (waitforeditor) {
            signal_start();
            tty_start();
        }
        errmsg("cannot start the editor");
        if (out_pipe[0] >= 0) {
            close(out_pipe[0]);
            close(out_pipe[1]);
        }
        unlink(tmpname);
        free(title);
        free(s->descr);
        free(s->file);
        free(s);
        return;
    }
    free(title);
    s->pid = childpid;
    if (out_pipe[0] >= 0) {
        close(out_pipe[1]);
        fcntl(out_pipe[0], F_SETFL, O_NONBLOCK);
        fcntl(out_pipe[0], F_SETFD, FD_CLOEXEC);
        s->out_fd = out_pipe[0];
    }
    if (waitforeditor) {
        int wpid;
        while ((wpid = waitpid(childpid, NULL, 0)) == -1 && errno == EINTR)
            ;

        signal_start();         /* reset SIGINT and SIGCHLD handlers */
        tty_start();
        /* the terminal echoed the keys typed after the editor restored
         * it and before tty_start(), such as ^P: erase them */
        tty_putc('\r');
        tty_puts(tty_clreoln);

        if (s->conn != NULL) {
            tty_gotoxy(0, lines - 1);
            tty_putc('\n');
        }

        if (wpid == -1)
            errmsg("cannot wait for the editor");
        else
            finish_edit(s);

        free(s->descr);
        free(s->file);
        if (wpid != -1 && edit_sess == NULL && *edit_end != '\0') {
            error = 0;
            parse_instruction(edit_end, false, false, true);
            history_done = 0;
        }

        free(s);
    } else {
        s->next = edit_sess;
        edit_sess = s;
    }
}

/*
 * show the complete lines of output from the background editor of
 * 'sp', and with 'all' also an incomplete last line
 */
static void show_edit_output(editsess *sp, bool all)
{
    static const char prefix[] = "#editor: ";
    size_t end = sp->out_len;

    if (!all) {
        while (end > 0 && sp->out_buf[end - 1] != '\n')
            --end;
    }
    if (end == 0)
        return;

    size_t nlines = 1;
    for (size_t i = 0; i < end; ++i) {
        if (sp->out_buf[i] == '\n')
            ++nlines;
    }
    char *msg = malloc(end + nlines * sizeof(prefix) + 1);
    if (msg == NULL) {
        out_of_memory();
        sp->out_len = 0;
        return;
    }

    char *d = msg;
    for (size_t i = 0; i < end; ) {
        const char *nl = memchr(sp->out_buf + i, '\n', end - i);
        size_t len = (nl != NULL ? (size_t)(nl - sp->out_buf) : end) - i;
        memcpy(d, prefix, sizeof(prefix) - 1);
        d += sizeof(prefix) - 1;
        memcpy(d, sp->out_buf + i, len);
        d += len;
        *d++ = '\n';
        i += len + 1;
    }
    *d = '\0';
    write_message(msg);
    free(msg);

    sp->out_len -= end;
    memmove(sp->out_buf, sp->out_buf + end, sp->out_len);
}

/* show what is left of the output of 'sp' and stop reading it */
static void close_edit_output(editsess *sp)
{
    show_edit_output(sp, true);
    close(sp->out_fd);
    sp->out_fd = -1;
}

/*
 * read the available output of the background editor of 'sp', closing
 * it at the end; return true if there may be more to read
 */
static bool read_edit_output(editsess *sp)
{
    ssize_t n = read(sp->out_fd, sp->out_buf + sp->out_len,
                     sizeof(sp->out_buf) - sp->out_len);
    if (n > 0) {
        sp->out_len += (size_t)n;
        show_edit_output(sp, sp->out_len == sizeof(sp->out_buf));
        return true;
    }
    if (n < 0 && errno == EINTR)
        return true;
    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
        return false;
    close_edit_output(sp);
    return false;
}

int edit_output_fds(fd_set *set, int max_fd)
{
    for (editsess *sp = edit_sess; sp != NULL; sp = sp->next) {
        if (sp->out_fd >= 0) {
            FD_SET(sp->out_fd, set);
            if (max_fd < sp->out_fd)
                max_fd = sp->out_fd;
        }
    }
    return max_fd;
}

int edit_read_output(const fd_set *set)
{
    int count = 0;
    for (editsess *sp = edit_sess; sp != NULL; sp = sp->next) {
        if (sp->out_fd >= 0 && FD_ISSET(sp->out_fd, set)) {
            ++count;
            read_edit_output(sp);
        }
    }
    return count;
}

/*
 * Our child has snuffed it. check if it was an editor, and update the
 * session list if that is the case.
 */
void sig_chld_bottomhalf(void)
{
    int pid;
    int ret;
    while ((pid = waitpid(-1, &ret, WNOHANG)) > 0) {
        /* no need to check WIFSTOPPED, since waitpid() has no WUNTRACED */
        editsess **sp = &edit_sess;
        while (*sp != NULL && (*sp)->pid != pid)
            sp = &(*sp)->next;
        if (*sp != NULL) {
            if ((*sp)->out_fd >= 0) {
                /*
                 * show what the editor printed last; do not wait for
                 * the end, as programs it started may still keep it open
                 */
                while ((*sp)->out_fd >= 0 && read_edit_output(*sp))
                    ;
                if ((*sp)->out_fd >= 0)
                    close_edit_output(*sp);
            }
            finish_edit(*sp);
            editsess *p = *sp;
            *sp = p->next;
            connsess *conn = p->conn;
            free(p->descr);
            free(p->file);
            free(p);

            /* send the edit_end message if this is the last editor... */
            if (edit_sess == NULL && *edit_end != '\0') {
                connsess *old_conn = current_connection;
                current_connection = conn;
                error = 0;
                parse_instruction(edit_end, false, false, true);
                history_done = 0;
                current_connection = old_conn;
            }
        }
    }
}
