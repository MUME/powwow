/*
 *  tty.c -- terminal handling routines for powwow
 *
 *  Copyright (C) 1998 by Massimiliano Ghilardi
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 */
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "defines.h"

#ifdef USE_LOCALE
#include <langinfo.h>
#include <locale.h>
#include <strings.h>
#include <wchar.h>
#endif

#include "main.h"
#include "edit.h"
#include "utils.h"
#include "list.h"
#include "tty.h"
#include "tcp.h"
#include "charset.h"
#include "width.h"

#include <sys/ioctl.h>
#include <termios.h>

#ifdef USE_VT100	/* hard-coded vt100 features if no termcap: */

/* terminal escape sequences */
static const char kpadstart[] = "";
static const char kpadend[] = "";
static const char begoln[] = "\r";
static const char clreoln[] = "\033[K";
static const char clreoscr[] = "\033[J";
static const char leftcur[] = "\033[D";
static const char rightcur[] = "\033[C";
static const char upcur[] = "\033[A";
static const char cursor_left[] = "\033[D";
static const char cursor_right[] = "\033[C";
static const char cursor_up[] = "\033[A";
static const char cursor_down[] = "\033[B";
static const char cursor_delete[] = "\033[3~";

/* attribute changers */
static const char modebold[] = "\033[1m";
static const char modeblink[] = "\033[5m";
static const char modeinv[] = "\033[7m";
static const char modeuline[] = "\033[4m";
static const char modestandon[] = "";
static const char modestandoff[] = "";
static const char modenorm[] = "\033[m";

#define insertfinish (0)
static int len_begoln = 1, len_leftcur = 3, len_upcur = 3, gotocost = 8;

#else /* not USE_VT100, termcap function declarations */

int tgetent(char *bp, const char *name);
int tgetnum(const char *id);
int tgetflag(const char *id);
char *tgetstr(const char *id, char **area);
char *tgoto(const char *cap, int col, int row);

/* terminal escape sequences, from termcap */
static char kpadstart[CAPLEN], kpadend[CAPLEN];
static char begoln[CAPLEN], clreoln[CAPLEN], clreoscr[CAPLEN];
static char leftcur[CAPLEN], rightcur[CAPLEN], upcur[CAPLEN];
static char curgoto[CAPLEN];
static char delchar[CAPLEN], insstart[CAPLEN], insstop[CAPLEN];
static char inschar[CAPLEN];
static char cursor_left[CAPLEN], cursor_right[CAPLEN];
static char cursor_up[CAPLEN], cursor_down[CAPLEN];
static char cursor_delete[CAPLEN];

/* attribute changers */
static char modebold[CAPLEN], modeblink[CAPLEN], modeinv[CAPLEN];
static char modeuline[CAPLEN], modestandon[CAPLEN], modestandoff[CAPLEN];
static char modenorm[CAPLEN];

static int len_begoln, len_clreoln, len_leftcur, len_upcur, gotocost;
static int deletecost, insertcost, insertfinish, inscharcost;

static int extract(char *cap, char *buf);

#endif /* USE_VT100 */


const char *tty_modebold = modebold;
const char *tty_modeblink = modeblink;
const char *tty_modeinv = modeinv;
const char *tty_modeuline = modeuline;
const char *tty_modestandon = modestandon;
const char *tty_modestandoff = modestandoff;
const char *tty_modenorm = modenorm;
const char *tty_begoln = begoln;
const char *tty_clreoln = clreoln;
const char *tty_clreoscr = clreoscr;

int tty_read_fd = 0;
static int wrapglitch = 0;

#ifdef USE_LOCALE
static FILE *tty_read_stream;
static int orig_read_fd_fl;
static struct {
    mbstate_t mbstate;              /* multibyte output shift state */
    char      data[4096];           /* buffer for pending data */
    size_t    used;                 /* bytes used of data */
    int       fd;                   /* file descriptor to write to */
    bool      utf8;                 /* the locale uses UTF-8 */
    char      pend[UTF8_MAX];       /* incomplete UTF-8 character */
    int       npend, need;          /* bytes in pend, and expected */
} tty_write_state = {
    .fd = 1                         /* also before tty_start() */
};
#endif  /* USE_LOCALE */

static struct termios ttybsave;

/*
 * disable the keys that the terminal handles itself even in non-canonical
 * mode, so that they can be bound: suspend (^Z, handled by powwow), and,
 * where they exist, delayed suspend (^Y) and discard output (^O)
 */
static void disable_special_keys(struct termios *ttyb)
{
    ttyb->c_cc[VSUSP] = _POSIX_VDISABLE;
#ifdef VDSUSP
    ttyb->c_cc[VDSUSP] = _POSIX_VDISABLE;
#endif
#ifdef VDISCARD
    ttyb->c_cc[VDISCARD] = _POSIX_VDISABLE;
#endif
}

/*
 * Set the terminal to character-at-a-time-without-echo mode, and save the
 * original state in ttybsave
 */
void tty_start(void)
{
    tcgetattr(tty_read_fd, &ttybsave);
    struct termios ttyb = ttybsave;
    ttyb.c_lflag &= ~(ECHO | ICANON);
    /* let ^S and ^Q through instead of stopping and starting the output */
    ttyb.c_iflag &= ~IXON;
    ttyb.c_cc[VTIME] = 0;
    ttyb.c_cc[VMIN] = 1;
    disable_special_keys(&ttyb);
    tcsetattr(tty_read_fd, TCSANOW, &ttyb);

#ifdef USE_LOCALE
    orig_read_fd_fl = fcntl(tty_read_fd, F_GETFL);
    fcntl(tty_read_fd, F_SETFL, O_NONBLOCK | orig_read_fd_fl);
#endif

    tty_puts(kpadstart);
    tty_flush();

#ifdef USE_LOCALE
    const char *codeset = nl_langinfo(CODESET);
    tty_write_state.utf8 = (strcasecmp(codeset, "UTF-8") == 0
                            || strcasecmp(codeset, "UTF8") == 0);
    if (wcrtomb(NULL, L'\0', &tty_write_state.mbstate) == (size_t)-1) {
        perror("wcrtomb()");
        abort();
    }
#else  /* ! USE_LOCALE */
 #ifdef DEBUG_TTY
    setvbuf(stdout, NULL, _IONBF, BUFSIZ);
 #else
    setvbuf(stdout, NULL, _IOFBF, BUFSIZ);
 #endif
#endif  /* ! USE_LOCALE */
}

/*
 * Reset the terminal to its original state
 */
void tty_quit(void)
{
    tcsetattr(tty_read_fd, TCSANOW, &ttybsave);
    tty_puts(kpadend);
    tty_flush();
#ifdef USE_LOCALE
    fcntl(tty_read_fd, F_SETFL, orig_read_fd_fl);
#endif
}

/*
 * enable/disable special keys depending on the current linemode
 */
void tty_special_keys(void)
{
    struct termios ttyb;
    tcgetattr(tty_read_fd, &ttyb);
    if (linemode & LM_CHAR) {
        /* char-by-char mode: disable all special keys and set raw mode */
        for (int i = 0; i < NCCS; ++i)
            ttyb.c_cc[i] = _POSIX_VDISABLE;
        ttyb.c_oflag &= ~OPOST;
    } else {
        /* line at a time mode: enable them, except those powwow handles */
        for (int i = 0; i < NCCS; ++i)
            ttyb.c_cc[i] = ttybsave.c_cc[i];
        disable_special_keys(&ttyb);
        /* set cooked mode */
        ttyb.c_oflag |= OPOST;
    }
    ttyb.c_cc[VTIME] = 0;
    ttyb.c_cc[VMIN] = 1;
    tcsetattr(tty_read_fd, TCSANOW, &ttyb);
}

/*
 * get window size and react to any window size change
 */
void tty_sig_winch_bottomhalf(void)
{
    struct winsize wsiz;
        /* if ioctl fails or gives silly values, don't change anything */

    if (ioctl(tty_read_fd, TIOCGWINSZ, &wsiz) == 0
	&& wsiz.ws_row > 0 && wsiz.ws_col > 0
	&& (lines != wsiz.ws_row || cols != wsiz.ws_col))
    {
	lines = wsiz.ws_row;
	cols_1 = cols = wsiz.ws_col;
	if (!wrapglitch)
	    cols_1--;

	if (main_connection != NULL)
	    tcp_write_tty_size();
	line0 += lines - olines;

	tty_gotoxy(0, line0);
	/* so we know where the cursor is */
#ifdef USE_ANSIBUG
	if (edattrbg)
	    tty_printf("%s%s", edattrend, tty_clreoscr);
	else
#endif
	    tty_puts(tty_clreoscr);

	olines = lines;
	status(1);
    }
}

/*
 * read termcap definitions
 */
void tty_bootstrap(void)
{
#ifdef USE_LOCALE
    tty_read_stream = stdin;
#endif

#ifndef USE_VT100
    struct tc_init_node {
	char cap[4], *buf;
	int *len, critic;
    };
    static struct tc_init_node tc_init[] = {
	{ "cm", curgoto, 0, 1 },
	{ "ce", clreoln, &len_clreoln, 1 },
	{ "cd", clreoscr, 0, 1 },
	{ "nd", rightcur, 0, 1 },
	{ "le", leftcur, &len_leftcur, 0 },
	{ "up", upcur,   &len_upcur,  0 },
	{ "cr", begoln,  &len_begoln, 0 },
	{ "ic", inschar, &inscharcost, 0 },
	{ "im", insstart, &insertcost, 0 },
	{ "ei", insstop, &insertcost, 0 },
	{ "dm", delchar, &deletecost, 0 },
	{ "dc", delchar, &deletecost, 0 },
	{ "ed", delchar, &deletecost, 0 },
	{ "me", modenorm, 0, 0 },
	{ "md", modebold, 0, 0 },
	{ "mb", modeblink, 0, 0 },
	{ "mr", modeinv, 0, 0 },
	{ "us", modeuline, 0, 0 },
	{ "so", modestandon, 0, 0 },
	{ "se", modestandoff, 0, 0 },
	{ "ks", kpadstart, 0, 0 },
	{ "ke", kpadend, 0, 0 },
	{ "kl", cursor_left, 0, 0 },
	{ "kr", cursor_right, 0, 0 },
	{ "ku", cursor_up, 0, 0 },
	{ "kd", cursor_down, 0, 0 },
	{ "kD", cursor_delete, 0, 0 },
	{ "", NULL, 0, 0 }
    };
    struct tc_init_node *np;
    char tcbuf[2048];		/* by convention, this is enough */
    int i;
    char *term = getenv("TERM");
    if (!term) {
	fprintf(stderr, "$TERM not set\n");
	exit(1);
    }
#endif /* not USE_VT100 */
#ifdef USE_VT100
    cols = 80;
#  ifdef LINES
    lines = LINES;
#  else  /* not LINES */
    lines = 24;
#  endif /* LINES */
#else   /* not USE_VT100 */
    switch(tgetent(tcbuf, term)) {
     case 1:
	break;
     case 0:
	fprintf(stderr,
		"There is no entry for \"%s\" in the terminal data base.\n", term);
	fprintf(stderr,
		"Please set your $TERM environment variable correctly.\n");
	exit(1);
     default:
	syserr("cannot read the terminal database");
    }
    for(np = tc_init; np->cap[0]; np++)
	if ((i = extract(np->cap, np->buf))) {
	    if (np->len) *np->len += i;
	} else if (np->critic) {
	    fprintf(stderr,
		    "Your \"%s\" terminal is not powerful enough, missing \"%s\".\n",
		    term, np->cap);
	    exit(1);
	}
    if (!len_begoln)
	strcpy(begoln, "\r"), len_begoln = 1;
    if (!len_leftcur)
	strcpy(leftcur, "\b"), len_leftcur = 1;

    gotocost = strlen(tgoto(curgoto, cols - 1, lines - 1));
    insertfinish = gotocost + len_clreoln;

    /* this must be before getting window size */
    wrapglitch = tgetflag("xn");

    tty_sig_winch_bottomhalf(); /* get window size */

#endif  /* not USE_VT100 */
}

/*
 * the default keypad keys, which walk around; termcap does not have their
 * sequences, so they are those of a vt100 (they can be changed with #bind
 * or #rebind)
 */
static const struct {
    const char *label, *seq, *command;
} walk_binds[] = {
    { .label = "KP2", .seq = "\033Or", .command = "s" },
    { .label = "KP3", .seq = "\033Os", .command = "d" },
    { .label = "KP4", .seq = "\033Ot", .command = "w" },
    { .label = "KP5", .seq = "\033Ou", .command = "exits" },
    { .label = "KP6", .seq = "\033Ov", .command = "e" },
    { .label = "KP7", .seq = "\033Ow", .command = "look" },
    { .label = "KP8", .seq = "\033Ox", .command = "n" },
    { .label = "KP9", .seq = "\033Oy", .command = "u" },
};

void tty_add_walk_binds(void)
{
    for (size_t i = 0; i < sizeof(walk_binds) / sizeof(*walk_binds); ++i)
        add_keynode(walk_binds[i].label, walk_binds[i].seq, 0,
                    key_run_command, walk_binds[i].command);
}

/*
 * the default editing keys, with the save-file version that added them
 * (see tty_add_new_binds); the cursor keys and Delete are bound only if
 * termcap has their sequences
 */
static const struct {
    const char *label, *seq;
    function_str funct;
    int since;
} default_binds[] = {
    { .label = "LF", .seq = "\n", .funct = enter_line },
    { .label = "Ret", .seq = "\r", .funct = enter_line },
    { .label = "C-h", .seq = "\b", .funct = del_char_left },
    { .label = "Del", .seq = "\177", .funct = del_char_left },
    { .label = "Tab", .seq = "\t", .funct = complete_word },
    { .label = "C-a", .seq = "\001", .funct = begin_of_line },
    { .label = "C-b", .seq = "\002", .funct = prev_char },
    { .label = "C-d", .seq = "\004", .funct = del_char_right },
    { .label = "C-e", .seq = "\005", .funct = end_of_line },
    { .label = "C-f", .seq = "\006", .funct = next_char },
    { .label = "C-g", .seq = "\007", .funct = abort_edit,
      .since = READLINE_KEYS_SAVEFILEVER },
    { .label = "C-k", .seq = "\013", .funct = kill_to_eol },
    { .label = "C-l", .seq = "\014", .funct = redraw_line },
    { .label = "C-n", .seq = "\016", .funct = next_line },
    { .label = "C-p", .seq = "\020", .funct = prev_line },
    { .label = "C-r", .seq = "\022", .funct = reverse_search,
      .since = READLINE_KEYS_SAVEFILEVER },
    { .label = "C-s", .seq = "\023", .funct = forward_search,
      .since = READLINE_KEYS_SAVEFILEVER },
    { .label = "C-t", .seq = "\024", .funct = transpose_chars },
    { .label = "C-u", .seq = "\025", .funct = kill_to_bol,
      .since = READLINE_KEYS_SAVEFILEVER },
    { .label = "C-w", .seq = "\027", .funct = del_unix_word_left },
    { .label = "C-y", .seq = "\031", .funct = yank,
      .since = READLINE_KEYS_SAVEFILEVER },
    { .label = "C-z", .seq = "\032", .funct = suspend_key },
    { .label = "M-Tab", .seq = "\033\t", .funct = complete_line },
    { .label = "M-b", .seq = "\033b", .funct = prev_word },
    { .label = "M-c", .seq = "\033c", .funct = capitalize_word,
      .since = READLINE_KEYS_SAVEFILEVER },
    { .label = "M-d", .seq = "\033d", .funct = del_word_right },
    { .label = "M-f", .seq = "\033f", .funct = next_word },
    { .label = "M-k", .seq = "\033k", .funct = redraw_line_noprompt },
    { .label = "M-t", .seq = "\033t", .funct = transpose_words },
    { .label = "M-u", .seq = "\033u", .funct = upcase_word },
    { .label = "M-y", .seq = "\033y", .funct = yank_pop,
      .since = READLINE_KEYS_SAVEFILEVER },
    { .label = "M-l", .seq = "\033l", .funct = downcase_word },
    { .label = "M-q", .seq = "\033q", .funct = to_history,
      .since = READLINE_KEYS_SAVEFILEVER },
    { .label = "M-C-h", .seq = "\033\b", .funct = del_word_left },
    { .label = "M-Del", .seq = "\033\177", .funct = del_word_left },
    { .label = "Left", .seq = cursor_left, .funct = prev_char },
    { .label = "Right", .seq = cursor_right, .funct = next_char },
    { .label = "Up", .seq = cursor_up, .funct = prev_line },
    { .label = "Down", .seq = cursor_down, .funct = next_line },
    { .label = "Delete", .seq = cursor_delete, .funct = del_char_right,
      .since = READLINE_KEYS_SAVEFILEVER },
};

/* is the key 'label' or the key sequence 'seq' bound? */
static bool key_is_bound(const char *label, const char *seq)
{
    int len = strlen(seq);
    for (const keynode *k = keydefs; k != NULL; k = k->next) {
        if (strcmp(k->name, label) == 0
            || (k->seqlen == len && memcmp(k->sequence, seq, len) == 0))
            return true;
    }
    return false;
}

/*
 * initialize the key binding list
 */
void tty_add_initial_binds(void)
{
    for (size_t i = 0; i < sizeof(default_binds) / sizeof(*default_binds);
         ++i) {
        if (*default_binds[i].seq != '\0')
            add_keynode(default_binds[i].label, default_binds[i].seq, 0,
                        default_binds[i].funct, NULL);
    }
}

/*
 * rename the key 'old' to 'new', if it has the sequence 'seq' and there
 * is no key 'new' yet
 */
static void rename_key(char *old, const char *seq, char *new)
{
    keynode *k = *lookup_key(old);
    int len = strlen(seq);
    if (k == NULL || k->seqlen != len || memcmp(k->sequence, seq, len) != 0
        || *lookup_key(new) != NULL)
        return;
    add_keynode(new, k->sequence, k->seqlen, k->funct, k->call_data);
    delete_keynode(lookup_key(old));
    tty_printf("#key %s renamed to %s\n", old, new);
}

/*
 * add the default editing keys added after save-file version 'version',
 * unless the save-file binds those keys, rename the keys whose default
 * names changed, and make C-w delete a word if it is still the old default
 * of &to-history
 */
void tty_add_new_binds(int version)
{
    if (version < READLINE_KEYS_SAVEFILEVER) {
        rename_key("BS", "\b", "C-h");
        rename_key("M-BS", "\033\b", "M-C-h");
    }

    char added[BUFSIZE];
    int n = 0;
    for (size_t i = 0; i < sizeof(default_binds) / sizeof(*default_binds);
         ++i) {
        if (default_binds[i].since <= version
            || *default_binds[i].seq == '\0'
            || key_is_bound(default_binds[i].label, default_binds[i].seq))
            continue;
        add_keynode(default_binds[i].label, default_binds[i].seq, 0,
                    default_binds[i].funct, NULL);
        n += snprintf(added + n, sizeof(added) - n, " %s",
                      default_binds[i].label);
    }
    if (n > 0)
        tty_printf("#new default editing keys added:%s\n", added);

    keynode *k = *lookup_key("C-w");
    if (version < READLINE_KEYS_SAVEFILEVER && k != NULL
        && k->funct == to_history && k->seqlen == 1 && k->sequence[0] == '\027'
        && k->call_data == NULL) {
        k->funct = del_unix_word_left;
        tty_puts("#C-w now deletes back to the previous space;"
                 " M-q puts the line in history\n");
    }
}

/*
 * find the default binding of the key 'label': set its sequence, and its
 * editing function or command; return false if it has none
 */
bool tty_default_bind(const char *label, const char **seq,
                      function_str *funct, const char **command)
{
    *command = NULL;
    for (size_t i = 0; i < sizeof(default_binds) / sizeof(*default_binds);
         ++i) {
        if (strcmp(default_binds[i].label, label) == 0
            && *default_binds[i].seq != '\0') {
            *seq = default_binds[i].seq;
            *funct = default_binds[i].funct;
            return true;
        }
    }
    for (size_t i = 0; i < sizeof(walk_binds) / sizeof(*walk_binds); ++i) {
        if (strcmp(walk_binds[i].label, label) == 0) {
            *seq = walk_binds[i].seq;
            *funct = key_run_command;
            *command = walk_binds[i].command;
            return true;
        }
    }
    return false;
}

#ifndef USE_VT100
/*
 * extract termcap 'cap' and strcat it to buf.
 * return the lenght of the extracted string.
 */
static int extract(char *cap, char *buf)
{
    static char *bp;
    char *d = buf + strlen(buf);
    char *s = tgetstr(cap, (bp = d, &bp));
    int len;
    if (!s) return (*bp = 0);
    /*
     * Remove the padding information. We assume that no terminals
     * need padding nowadays. At least it makes things much easier.
     */
    s += strspn(s, "0123456789*");
    for(len = 0; *s; *d++ = *s++, len++)
	if (*s == '$' && *(s + 1) == '<')
	if (!(s = strchr(s, '>')) || !*++s) break;
    *d = 0;
    return len;
}
#endif /* not USE_VT100 */

/*
 * position the cursor using absolute coordinates
 * note: does not flush the output buffer
 */
void tty_gotoxy(int col, int line)
{
#ifdef USE_VT100
    tty_printf("\033[%d;%dH", line + 1, col + 1);
#else
    tty_puts(tgoto(curgoto, col, line));
#endif
}

/*
 * optimized cursor movement
 * from (fromcol, fromline) to (tocol, toline)
 * if tocol > 0, (tocol, toline) must lie on editline.
 */
void tty_gotoxy_opt(int fromcol, int fromline, int tocol, int toline)
{
    static char buf[BUFSIZE];
    char *cp = buf;
    int cost, i, dist;

    CLIP(fromline, 0, lines-1);
    CLIP(toline  , 0, lines-1);

    /* First, move vertically to the correct line, then horizontally
     * to the right column. If this turns out to be fewer characters
     * than a direct cursor positioning (tty_gotoxy), use that.
     */
    for (;;) {	/* gotoless */
	if ((i = toline - fromline) < 0) {
	    if (!len_upcur || (cost = -i * len_upcur) >= gotocost)
		break;
	    do {
		strcpy(cp, upcur);
	        cp += len_upcur;
	    } while(++i);
	} else if ((cost = 2 * i)) {	/* lf is mapped to crlf on output */
	    if (cost >= gotocost)
		break;
	    do
	        *cp++ = '\n';
	    while (--i);
	    fromcol = 0;
	}
	if ((i = tocol - fromcol) < 0) {
	    dist = -i * len_leftcur;
	    if (dist <= len_begoln + tocol) {
		if ((cost += dist) > gotocost)
		    break;
		do {
		    strcpy(cp, leftcur);
		    cp += len_leftcur;
		} while(++i);
	    } else {
		if ((cost += len_begoln) > gotocost)
		    break;
		strcpy(cp, begoln);
		cp += len_begoln;
		fromcol = 0;  i = tocol;
	    }
	}
	if (i) {
	    /* moving right would mean reprinting text whose width is
	     * not one column per byte; just use tty_gotoxy */
	    break;
	}
	*cp = 0;
	tty_puts(buf);
	return;
    }
    tty_gotoxy(tocol, toline);
}


/*
 * does the cell of 'n' bytes at 's', 'w' columns wide, fit in a row
 * of the screen at column 'col'?
 */
int tty_cell_fits(int col, const char *s, size_t n, int w)
{
    /*
     * Terminal.app draws only the first characters of a grapheme
     * cluster that ends in the last column, and the rest (a VS16, or
     * what follows a ZWJ) on the next row: keep those out of it
     */
    if (col > 0 && col + w == cols && n > (size_t)utf8_seqlen(*s))
        return 0;
    return col + w <= cols_1;
}

/*
 * screen position where the text before byte offset 'p' of the input
 * line ends; p <= 0 refers to the prompt
 */
static void input_layout_end(int p, int *line, int *col)
{
    if (p <= 0) {
        *line = (p + col0) / cols_1 + line0;
        *col = (p + col0) % cols_1;
        return;
    }

    int l = col0 / cols_1, c = col0 % cols_1;
    const char *s = edbuf, *end = edbuf + p;
    while (s < end) {
        size_t n = utf8_cell_len(s, end - s);
        int w = utf8_cell_width(s, n);
        if (!tty_cell_fits(c, s, n, w)) {
            /* does not fit; drawn on the next row */
            l++;
            c = 0;
        }
        c += w;
        s += n;
    }
    if (c >= cols_1) {
        l++;
        c = 0;
    }
    *line = l + line0;
    *col = c;
}

/*
 * screen position of the cell at byte offset 'p' of the input line,
 * where the cursor is when it is there: a wide character that does not
 * fit at the end of a row is on the next one
 */
static void input_layout(int p, int *line, int *col)
{
    input_layout_end(p, line, col);
    if (p >= 0 && p < edlen && *col > 0) {
        size_t n = utf8_cell_len(edbuf + p, edlen - p);
        if (!tty_cell_fits(*col, edbuf + p, n,
                           utf8_cell_width(edbuf + p, n))) {
            ++*line;
            *col = 0;
        }
    }
}

int input_line(int p)
{
    int line, col;
    input_layout(p, &line, &col);
    return line;
}

int input_col(int p)
{
    int line, col;
    input_layout(p, &line, &col);
    return col;
}

/*
 * move the cursor from pos to 'toline', 'tocol', scrolling the input
 * line if that is off the screen; with 'absolute', do not rely on the
 * cursor being at pos
 */
static void input_cursor_to(int toline, int tocol, bool absolute)
{
    int fromline = CURLINE(pos);
    if (toline < 0)
	line0 -= toline, toline = 0;
    else if (toline > lines - 1)
	line0 -= toline - lines + 1, toline = lines - 1;
    if (absolute)
        tty_gotoxy(tocol, toline);
    else
        tty_gotoxy_opt(CURCOL(pos), fromline, tocol, toline);
}

void input_moveto(int new_pos)
{
    /*
     * FEATURE: the line we are moving to might be less than 0, or greater
     * than lines - 1, if the display is too small to hold the whole editline.
     * In that case, the input line should be (partially) redrawn.
     */
    if (new_pos < 0)
	new_pos = 0;
    else if (new_pos > edlen)
	new_pos = edlen;
    if (new_pos == pos)
	return;

    if (line_status == 0)
	input_cursor_to(CURLINE(new_pos), CURCOL(new_pos), false);
    pos = new_pos;
}

/*
 * start a new screen line while drawing the input line
 */
static void input_newline(int *line)
{
#ifdef USE_ANSIBUG
    if (edattrbg)
	tty_printf("%s\n%s", edattrend, edattrbeg);
    else
#endif
	tty_putc('\n');
    if (++*line > lines - 1) {
	*line = lines - 1;
	line0--;
    }
}

/*
 * erase the screen from the cursor, at 'line', 'col', to the end of the
 * input line
 */
static void input_erase_below(int line, int col)
{
    if (edattrbg)
        tty_puts(edattrend);
    if (wrapglitch && col == cols - 1) {
        /*
         * iTerm2 erases the character before the last column too, if
         * the last column held the right half of a wide character
         * whose left half was overwritten: overwrite that half first
         * (the cursor stays in the last column)
         */
        tty_putc(' ');
    }
    tty_puts(line < lines - 1 ? tty_clreoscr : tty_clreoln);
    if (edattrbg)
        tty_puts(edattrbeg);
}

/*
 * redraw the input line from byte offset 'from' to its end, clear the
 * rest of the screen, and put the cursor at pos; the cursor must be
 * where the text before 'from' ends (see input_redraw_goto)
 */
static void input_redraw_from(int from)
{
    int line, col;
    input_layout_end(from, &line, &col);

    const char *s = edbuf + from, *end = edbuf + edlen;
    while (s < end) {
        size_t n = utf8_cell_len(s, end - s);
        int w = utf8_cell_width(s, n);
        if (!tty_cell_fits(col, s, n, w)) {
            if (col < cols_1) {
                /* a wide character did not fit; blank the rest of
                 * the row, which may hold half of an old one (with
                 * spaces: see input_erase_below) */
                if (edattrbg)
                    tty_puts(edattrend);
                for (; col < cols_1; col++)
                    tty_putc(' ');
                if (edattrbg)
                    tty_puts(edattrbeg);
            }
            input_newline(&line);
            col = 0;
        }
        while (n-- > 0)
            tty_putc(*s++);
        col += w;
    }
    if (col >= cols_1) {
        input_newline(&line);
        col = 0;
    }

    input_erase_below(line, col);

    int pline, pcol;
    input_layout(pos, &pline, &pcol);
    tty_gotoxy_opt(col, line, pcol, pline);
}

/*
 * draw the whole input line; the cursor is assumed directly after the
 * prompt
 */
void input_draw(void)
{
    input_redraw_from(0);
}

/*
 * shorten 'n' bytes of UTF-8 at 'str' to at most 'max' bytes, without
 * splitting a character
 */
static int input_fit(const char *str, int n, int max)
{
    if (n <= max)
        return n;
    n = max;
    while (n > 0 && ((unsigned char)str[n] & 0xc0) == 0x80)
        n--;
    return n;
}

/*
 * start of the cell before byte offset 'p' of the input line: a change
 * at 'p' may join text to that cell, so it must be redrawn too
 */
static int input_redraw_start(int p)
{
    return utf8_prev_cell(edbuf, edbuf + p) - edbuf;
}

/*
 * move the cursor from pos to where the text before byte offset 'from'
 * ends, to redraw from there; that is not where the cell at 'from' is
 * if it is a wide character that wrapped to the next row.  Move there
 * directly: if the terminal drew some text with another width than we
 * think, the redraw then repairs it
 */
static void input_redraw_goto(int from)
{
    if (line_status == 0) {
        int line, col;
        input_layout_end(from, &line, &col);
        input_cursor_to(line, col, true);
    }
}

/*
 * delete n bytes at current position (the position is unchanged)
 * assert(n < edlen - pos)
 */
void input_delete_nofollow_chars(int n)
{
    int p = pos, from = input_redraw_start(pos);

    if (n > edlen - pos)
	n = edlen - pos;
    if (n <= 0)
	return;

    input_redraw_goto(from);
    memmove(edbuf + p, edbuf + p + n, edlen - p - n + 1);
    edlen -= n;
    pos = p;

    if (line_status == 0)
        input_redraw_from(from);
}

/*
 * insert n bytes of UTF-8 at input line current position.
 * The position is set to after the inserted characters.
 */
void input_insert_follow_chars(char *str, int n)
{
    int p = pos, from = input_redraw_start(pos);

    n = input_fit(str, n, BUFSIZE - edlen - 1);
    if (n <= 0)
	return;

    input_redraw_goto(from);
    memmove(edbuf + p + n, edbuf + p, edlen + 1 - p);
    memmove(edbuf + p, str, n);
    edlen += n; pos = p + n;

    if (line_status == 0)
        input_redraw_from(from);
}

/*
 * replace 'len' bytes at byte offset 'from' of the input line with 'n'
 * bytes of UTF-8 from 'str'; the position is set to after them
 */
void input_replace(int from, int len, const char *str, int n)
{
    int start = input_redraw_start(from);

    input_redraw_goto(start);

    n = input_fit(str, n, BUFSIZE - 1 - (edlen - len));
    memmove(edbuf + from + n, edbuf + from + len, edlen - from - len + 1);
    memcpy(edbuf + from, str, n);
    edlen += n - len;
    pos = from + n;

    if (line_status == 0)
        input_redraw_from(start);
}

#ifdef USE_LOCALE
/* curses wide character support by Dain */

void tty_puts(const char *s)
{
    while (*s)
        tty_putc(*s++);
}

/* write character 'cp' in the locale's charset */
static void tty_put_wc(uint32_t cp)
{
    size_t r;
    int ignore_error = 0;
    wchar_t wc = cp;
    if (tty_write_state.used + MB_LEN_MAX > sizeof tty_write_state.data)
        tty_flush();
again:
    r = wcrtomb(tty_write_state.data + tty_write_state.used, wc,
                &tty_write_state.mbstate);
    if (r == (size_t)-1) {
        if (ignore_error)
            return;
        if (errno != EILSEQ) {
            perror("wcrtomb()");
            abort();
        }
        /* character cannot be represented; try to write a question
         * mark instead, but ignore any errors */
        ignore_error = 1;
        wc = '?';
        goto again;
    }
    assert(r <= MB_LEN_MAX);
    tty_write_state.used += r;
}

/* write one byte of UTF-8 text */
void tty_putc(char c)
{
    unsigned char b = c;

    if (tty_write_state.utf8) {
        if (tty_write_state.used >= sizeof tty_write_state.data)
            tty_flush();
        tty_write_state.data[tty_write_state.used++] = b;
        return;
    }

    if (tty_write_state.npend > 0) {
        if ((b & 0xc0) == 0x80) {
            tty_write_state.pend[tty_write_state.npend++] = b;
            if (tty_write_state.npend < tty_write_state.need)
                return;
            uint32_t cp;
            utf8_get(tty_write_state.pend, tty_write_state.npend, &cp);
            tty_write_state.npend = 0;
            tty_put_wc(cp);
            return;
        }
        /* incomplete character */
        tty_write_state.npend = 0;
        tty_put_wc(UNICODE_REPLACEMENT);
    }

    if (b < 0x80) {
        tty_put_wc(b);
        return;
    }
    tty_write_state.need = utf8_seqlen(b);
    if (tty_write_state.need <= 1) {
        tty_put_wc(UNICODE_REPLACEMENT);
        return;
    }
    tty_write_state.pend[0] = b;
    tty_write_state.npend = 1;
}

int tty_printf(const char *format, ...)
{
    char buf[1024], *bufp = buf;
    va_list va;
    int res;

    char *old_locale = strdup(setlocale(LC_ALL, NULL));

    setlocale(LC_ALL, "C");

    va_start(va, format);
    res = vsnprintf(buf, sizeof buf, format, va);
    va_end(va);

    /* a longer text is formatted again into big */
    bool too_long = (res >= 0 && (size_t)res >= sizeof(buf));
    char big[too_long ? res + 1 : 1];
    if (too_long) {
        bufp = big;
        va_start(va, format);
        vsnprintf(big, sizeof(big), format, va);
        va_end(va);
        assert(strlen(big) == (size_t)res);
    }

    setlocale(LC_ALL, old_locale);
    free(old_locale);

    tty_puts(bufp);

    return res;
}

/* bytes read from the terminal, not yet converted: a whole paste, so
 * that it is inserted at once (see insert_flush) */
static char tty_in_buf[BUFSIZE];
static size_t tty_in_start = 0, tty_in_used = 0;

/* UTF-8 of a character read, not yet returned by tty_read() */
static char tty_out_buf[UTF8_MAX];
static size_t tty_out_start = 0, tty_out_used = 0;

int tty_has_chars(void)
{
    return tty_in_used != tty_in_start || tty_out_used != tty_out_start;
}

static int safe_mbtowc(wchar_t *pwc, char *s, size_t *n)
{
    static mbstate_t ps;

    size_t r;

again:
    r = mbrtowc(pwc, s, *n, &ps);
    if (r == (size_t)-2) {
        /* incomplete character */
        *n = 0;
        return -1;
    }
    if (r == (size_t)-1 && errno == EILSEQ) {
        /* invalid character */
        --*n;
        memmove(s, s + 1, *n);
        goto again;
    }
    if (r == 0 && n > 0 && s[0] == 0) {
        *pwc = L'\0';
        r = 1;
    }
    return r;
}

/*
 * read up to 'count' bytes of UTF-8 from the terminal, converting
 * from the locale's charset
 */
int tty_read(char *buf, size_t count)
{
    int result = 0;

    for (;;) {
        while (count > 0 && tty_out_start < tty_out_used) {
            *buf++ = tty_out_buf[tty_out_start++];
            --count;
            ++result;
        }
        if (count == 0)
            break;

        if (tty_in_used > tty_in_start) {
            wchar_t wc;
            size_t avail = tty_in_used - tty_in_start;
            int converted = safe_mbtowc(&wc, tty_in_buf + tty_in_start,
                                        &avail);
            tty_in_used = tty_in_start + avail;
            if (converted >= 0) {
                if (converted == 0)
                    converted = 1;
                tty_in_start += converted;
                tty_out_start = 0;
                tty_out_used = utf8_put(wc, tty_out_buf);
                continue;
            }
        }
        if (result > 0)
            break;

        /* keep an incomplete character at the start of the buffer */
        memmove(tty_in_buf, tty_in_buf + tty_in_start,
                tty_in_used - tty_in_start);
        tty_in_used -= tty_in_start;
        tty_in_start = 0;
        int did_read = read(tty_read_fd, tty_in_buf + tty_in_used,
                            sizeof tty_in_buf - tty_in_used);
        if (did_read < 0 && errno == EINTR)
            continue;
        if (did_read <= 0)
            break;
        tty_in_used += did_read;
    }

    return result;
}


void tty_gets(char *s, int size)
{
    wchar_t ws[size];
    char *end = s + size - 1;

    if (!fgetws(ws, size, stdin))
	return;

    for (const wchar_t *w = ws; *w != L'\0'; ++w) {
        char buf[UTF8_MAX];
        int n = utf8_put(*w, buf);
        if (s + n > end)
            break;
        memcpy(s, buf, n);
        s += n;
    }
    *s = '\0';
}

void tty_flush(void)
{
    size_t n = tty_write_state.used;
    char *data = tty_write_state.data;
    while (n > 0) {
        ssize_t r;
        for (;;) {
            r = write(tty_write_state.fd, data, n);
            if (r >= 0)
                break;
            if (errno == EINTR)
                continue;
            if (errno != EAGAIN) {
                fprintf(stderr, "Cannot write to tty: %s\n", strerror(errno));
                abort();
            }
            fd_set wfds;
            FD_ZERO(&wfds);
            FD_SET(tty_write_state.fd, &wfds);
            do {
                r = select(tty_write_state.fd + 1, NULL, &wfds, NULL, NULL);
            } while (r < 0 && errno == EINTR);
            if (r <= 0) {
                fprintf(stderr, "Cannot write to tty; select failed: %s\n",
                        r == 0 ? "returned zero" : strerror(errno));
                abort();
            }
        }
        if (r < 0) {
            fprintf(stderr, "Cannot write to tty: %s\n", strerror(errno));
            abort();
        }
        n -= r;
        data += r;
    }
    tty_write_state.used = 0;
}

void tty_raw_write(char *data, size_t len)
{
    if (len == 0) return;

    for (;;) {
        size_t s = sizeof tty_write_state.data - tty_write_state.used;
        if (s > len) s = len;
        memcpy(tty_write_state.data + tty_write_state.used, data, s);
        len -= s;
        tty_write_state.used += s;
        if (len == 0)
            break;
        data += s;
        tty_flush();
    }
}

#endif /* USE_LOCALE */
