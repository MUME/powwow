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
#include <alloca.h>
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

#ifndef USE_SGTTY
#  ifdef APOLLO
#    include "/sys5.3/usr/include/sys/termio.h"
#  else
/*
 * including both termio.h and termios.h might be an overkill, and gives
 * many warnings, but seems to be necessary at times. works anyway.
 */
#    include <termios.h>
#    include <termio.h>
#  endif
/* #else USE_SGTTY */
#endif

/*
 * SunOS 4 doesn't have function headers and has the defs needed from
 * ioctl.h in termios.h.  Does it compile with USE_SGTTY?
 */
#if (defined(sun) && defined(sparc) && ! defined(__SVR4))
   extern int printf();
#else
#  include <sys/ioctl.h>
#endif

#ifdef USE_BSD_LIKE
#  include <sys/ioctl_compat.h>
#  define O_RAW RAW
#  define O_ECHO ECHO
#  define O_CBREAK CBREAK
#endif

#if defined(TCSETS) || defined(TCSETATTR)
#  ifndef TCSETS		/* cc for HP-UX  SHOULD define this... */
#    define TCSETS TCSETATTR
#    define TCGETS TCGETATTR
#  endif
typedef struct termios termiostruct;
#else
#  define TCSETS TCSETA
#  define TCGETS TCGETA
typedef struct termio termiostruct;
#endif

#ifdef VSUSP
#  define O_SUSP VSUSP
#else
#  ifdef SWTCH
#    define O_SUSP SWTCH
#  else
#    define O_SUSP SUSP
#  endif
#endif

#ifdef USE_VT100	/* hard-coded vt100 features if no termcap: */

static char kpadstart[] = "", kpadend[] = "", begoln[] = "\r",
	clreoln[] = "\033[K", clreoscr[] = "\033[J",
	leftcur[] = "\033[D", rightcur[] = "\033[C",  upcur[] = "\033[A",
	modebold[] = "\033[1m", modeblink[] = "\033[5m", modeinv[] = "\033[7m",
	modeuline[] = "\033[4m", modestandon[] = "", modestandoff[] = "",
        modenorm[] = "\033[m", modenormbackup[4],
	cursor_left[] = "\033[D", cursor_right[] = "\033[C",
        cursor_up[] = "\033[A", cursor_down[] = "\033[B";

#define insertfinish (0)
static int len_begoln = 1, len_leftcur = 3, len_upcur = 3, gotocost = 8;

#else /* not USE_VT100, termcap function declarations */

int tgetent(char *bp, const char *name);
int tgetnum(const char *id);
int tgetflag(const char *id);
char *tgetstr(const char *id, char **area);
char *tgoto(const char *cap, int col, int row);

/* terminal escape sequences */
static char kpadstart[CAPLEN], kpadend[CAPLEN],
	leftcur[CAPLEN], rightcur[CAPLEN], upcur[CAPLEN], curgoto[CAPLEN],
	delchar[CAPLEN], insstart[CAPLEN], insstop[CAPLEN],
        inschar[CAPLEN],
	begoln[CAPLEN], clreoln[CAPLEN], clreoscr[CAPLEN],
	cursor_left[CAPLEN], cursor_right[CAPLEN], cursor_up[CAPLEN],
	cursor_down[CAPLEN];

/* attribute changers: */
static char modebold[CAPLEN], modeblink[CAPLEN], modeinv[CAPLEN],
            modeuline[CAPLEN], modestandon[CAPLEN], modestandoff[CAPLEN],
            modenorm[CAPLEN], modenormbackup[CAPLEN];

static int len_begoln, len_clreoln, len_leftcur, len_upcur, gotocost,
           deletecost, insertcost, insertfinish, inscharcost;

static int extract(char *cap, char *buf);

#endif /* USE_VT100 */


char *tty_modebold = modebold,       *tty_modeblink = modeblink,
     *tty_modeinv  = modeinv,        *tty_modeuline = modeuline,
     *tty_modestandon = modestandon, *tty_modestandoff = modestandoff,
     *tty_modenorm = modenorm,       *tty_modenormbackup = modenormbackup,
     *tty_begoln   = begoln,         *tty_clreoln = clreoln,
     *tty_clreoscr = clreoscr;

int tty_read_fd = 0;
static int wrapglitch = 0;

#ifdef USE_LOCALE
FILE *tty_read_stream;
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

#ifdef USE_SGTTY
static struct sgttyb ttybsave;
static struct tchars tcsave;
static struct ltchars ltcsave;
#else /* not USE_SGTTY */
static termiostruct ttybsave;
#endif /* USE_SGTTY */

/*
 * Terminal handling routines:
 * These are one big mess of left-justified chicken scratches.
 * It should be handled more cleanly...but unix portability is what it is.
 */

/*
 * Set the terminal to character-at-a-time-without-echo mode, and save the
 * original state in ttybsave
 */
void tty_start(void)
{
#ifdef USE_SGTTY
    struct sgttyb ttyb;
    struct ltchars ltc;
    ioctl(tty_read_fd, TIOCGETP, &ttybsave);
    ioctl(tty_read_fd, TIOCGETC, &tcsave);
    ioctl(tty_read_fd, TIOCGLTC, &ltcsave);
    ttyb = ttybsave;
    ttyb.sg_flags = (ttyb.sg_flags|O_CBREAK) & ~O_ECHO;
    ioctl(tty_read_fd, TIOCSETP, &ttyb);
    ltc = ltcsave;
    ltc.t_suspc = -1;
    ioctl(tty_read_fd, TIOCSLTC, &ltc);
#else /* not USE_SGTTY */
    termiostruct ttyb;
    ioctl(tty_read_fd, TCGETS, &ttyb);
    ttybsave = ttyb;
    ttyb.c_lflag &= ~(ECHO|ICANON);
    ttyb.c_cc[VTIME] = 0;
    ttyb.c_cc[VMIN] = 1;
    /* disable the special handling of the suspend key (handle it ourselves) */
    ttyb.c_cc[O_SUSP] = 0;
    ioctl(tty_read_fd, TCSETS, &ttyb);
#endif /* USE_SGTTY */

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
#ifdef USE_SGTTY
    ioctl(tty_read_fd, TIOCSETP, &ttybsave);
    ioctl(tty_read_fd, TIOCSETC, &tcsave);
    ioctl(tty_read_fd, TIOCSLTC, &ltcsave);
#else /* not USE_SGTTY */
    ioctl(tty_read_fd, TCSETS, &ttybsave);
#endif /* USE_SGTTY */
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
#ifdef USE_SGTTY
    struct tchars tc = {-1, -1, -1, -1, -1, -1};
    struct ltchars ltc = {-1, -1, -1, -1, -1, -1};
    struct sgttyb ttyb;
    ioctl(tty_read_fd, TIOCGETP, &ttyb);
    if (linemode & LM_CHAR) {
	/* char-by-char mode: set RAW mode*/
	ttyb.sg_flags |= RAW;
    } else {
	/* line-at-a-time mode: enable spec keys, disable RAW */
	tc = tcsave;
	ltc = ltcsave;
	ltc.t_suspc = -1;	/* suspend key remains disabled */
	ttyb.sg_flags &= ~RAW;
    }
    ioctl(tty_read_fd, TIOCSETP, &ttyb);
    ioctl(tty_read_fd, TIOCSETC, &tc);
    ioctl(tty_read_fd, TIOCSLTC, &ltc);
#else /* not USE_SGTTY */
    int i;
    termiostruct ttyb;
    ioctl(tty_read_fd, TCGETS, &ttyb);
    if (linemode & LM_CHAR)  {
	/* char-by-char mode: disable all special keys and set raw mode */
	for(i = 0; i < NCCS; i++)
	    ttyb.c_cc[i] = 0;
	ttyb.c_oflag &= ~OPOST;
    } else {
	/* line at a time mode: enable them, except suspend */
	for(i = 0; i < NCCS; i++)
	    ttyb.c_cc[i] = ttybsave.c_cc[i];
	/* disable the suspend key (handle it ourselves) */
	ttyb.c_cc[O_SUSP] = 0;
	/* set cooked mode */
	ttyb.c_oflag |= OPOST;
    }
    ioctl(tty_read_fd, TCSETS, &ttyb);
#endif /* USE_SGTTY */
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
	{ "", NULL, 0, 0 }
    };
    struct tc_init_node *np;
    char tcbuf[2048];		/* by convention, this is enough */
    int i;
#endif /* not USE_VT100 */
#if !defined(USE_VT100) || defined(BUG_TELNET)
    char *term = getenv("TERM");
    if (!term) {
	fprintf(stderr, "$TERM not set\n");
	exit(1);
    }
#endif /* !defined(USE_VT100) || defined(BUG_TELNET) */
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
	syserr("tgetent");
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
    strcpy(modenormbackup, modenorm);
#ifdef BUG_TELNET
    if (strncmp(term, "vt10", 4) == 0) {
	/* might be NCSA Telnet 2.2 for PC, which doesn't reset colours */
	sprintf(modenorm, "\033[;%c%d;%s%dm",
		DEFAULTFG<LOWCOLORS ? '3' : '9',  DEFAULTFG % LOWCOLORS,
		DEFAULTBG<LOWCOLORS ? "4" : "10", DEFAULTBG % LOWCOLORS);
    }
#endif /* BUG_TELNET */
}

/*
 * add the default keypad bindings to the list
 */
void tty_add_walk_binds(void)
{
    /*
     * Note: termcap doesn't have sequences for the numeric keypad, so we just
     * assume they are the same as for a vt100. They can be redefined
     * at runtime anyway (using #bind or #rebind)
     */
    add_keynode("KP2", "\033Or", 0, key_run_command, "s");
    add_keynode("KP3", "\033Os", 0, key_run_command, "d");
    add_keynode("KP4", "\033Ot", 0, key_run_command, "w");
    add_keynode("KP5", "\033Ou", 0, key_run_command, "exits");
    add_keynode("KP6", "\033Ov", 0, key_run_command, "e");
    add_keynode("KP7", "\033Ow", 0, key_run_command, "look");
    add_keynode("KP8", "\033Ox", 0, key_run_command, "n");
    add_keynode("KP9", "\033Oy", 0, key_run_command, "u");
}

/*
 * initialize the key binding list
 */
void tty_add_initial_binds(void)
{
    struct b_init_node {
	char *label, *seq;
	function_str funct;
    };
    static struct b_init_node b_init[] = {
	{ "LF",		"\n",		enter_line },
	{ "Ret",	"\r",		enter_line },
	{ "BS",		"\b",		del_char_left },
	{ "Del",	"\177",		del_char_left },
	{ "Tab",	"\t",		complete_word },
	{ "C-a",	"\001",		begin_of_line },
	{ "C-b",	"\002",		prev_char },
	{ "C-d",	"\004",		del_char_right },
	{ "C-e",	"\005",		end_of_line },
	{ "C-f",	"\006",		next_char },
	{ "C-k",	"\013",		kill_to_eol },
	{ "C-l",	"\014",		redraw_line },
	{ "C-n",	"\016",		next_line },
	{ "C-p",	"\020",		prev_line },
	{ "C-t",	"\024",		transpose_chars },
	{ "C-w",	"\027",		to_history },
	{ "C-z",	"\032",		suspend_key },
	{ "M-Tab",	"\033\t",	complete_line },
	{ "M-b",	"\033b",	prev_word },
	{ "M-d",	"\033d",	del_word_right },
	{ "M-f",	"\033f",	next_word },
	{ "M-k",	"\033k",	redraw_line_noprompt },
	{ "M-t",	"\033t",	transpose_words },
	{ "M-u",	"\033u",	upcase_word },
	{ "M-l",	"\033l",	downcase_word },
	{ "M-BS",	"\033\b",	del_word_left },
	{ "M-Del",	"\033\177",	del_word_left },
	{ "",		"",		0 }
    };
    struct b_init_node *p = b_init;
    do {
	add_keynode(p->label, p->seq, 0, p->funct, NULL);
    } while((++p)->seq[0]);

    if (*cursor_left ) add_keynode("Left" , cursor_left , 0, prev_char, NULL);
    if (*cursor_right) add_keynode("Right", cursor_right, 0, next_char, NULL);
    if (*cursor_up   ) add_keynode("Up"   , cursor_up   , 0, prev_line, NULL);
    if (*cursor_down ) add_keynode("Down" , cursor_down , 0, next_line, NULL);
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
        s += n;
        if (c + w > cols_1) {
            /* does not fit; the terminal wraps it */
            l++;
            c = 0;
        }
        c += w;
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
        if (*col + utf8_cell_width(edbuf + p, n) > cols_1) {
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
 * GH: change the position on input line (gotoxy there, and set pos)
 *     from cancan 2.6.3a
 */
/*
 * move the cursor from pos to 'toline', 'tocol', scrolling the input
 * line if that is off the screen
 */
static void input_cursor_to(int toline, int tocol)
{
    int fromline = CURLINE(pos);
    if (toline < 0)
	line0 -= toline, toline = 0;
    else if (toline > lines - 1)
	line0 -= toline - lines + 1, toline = lines - 1;
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
	input_cursor_to(CURLINE(new_pos), CURCOL(new_pos));
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
        if (col + w > cols_1) {
            if (col < cols_1) {
                /* a wide character did not fit; clear the rest of
                 * the row, which may hold half of an old one */
                if (edattrbg)
                    tty_puts(edattrend);
                tty_puts(tty_clreoln);
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

    if (edattrbg)
        tty_puts(edattrend);
    tty_puts(line < lines - 1 ? tty_clreoscr : tty_clreoln);
    if (edattrbg)
        tty_puts(edattrbeg);

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
 * if it is a wide character that wrapped to the next row
 */
static void input_redraw_goto(int from)
{
    if (line_status == 0) {
        int line, col;
        input_layout_end(from, &line, &col);
        input_cursor_to(line, col);
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

    if (res >= sizeof buf) {
	bufp = alloca(res + 1);
	va_start(va, format);
	vsprintf(bufp, format, va);
        assert(strlen(bufp) == res);
	va_end(va);
    }

    setlocale(LC_ALL, old_locale);
    free(old_locale);

    tty_puts(bufp);

    return res;
}

static char tty_in_buf[MB_LEN_MAX + 1];
static size_t tty_in_buf_used = 0;

/* UTF-8 of a character read, not yet returned by tty_read() */
static char tty_out_buf[UTF8_MAX];
static size_t tty_out_start = 0, tty_out_used = 0;

int tty_has_chars(void)
{
    return tty_in_buf_used != 0 || tty_out_used != tty_out_start;
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

        if (tty_in_buf_used > 0) {
            wchar_t wc;
            int converted = safe_mbtowc(&wc, tty_in_buf, &tty_in_buf_used);
            if (converted >= 0) {
                if (converted == 0)
                    converted = 1;
                tty_in_buf_used -= converted;
                memmove(tty_in_buf, tty_in_buf + converted, tty_in_buf_used);
                tty_out_start = 0;
                tty_out_used = utf8_put(wc, tty_out_buf);
                continue;
            }
        }
        if (result > 0)
            break;

        int did_read = read(tty_read_fd, tty_in_buf + tty_in_buf_used,
                            sizeof tty_in_buf - tty_in_buf_used);
        if (did_read < 0 && errno == EINTR)
            continue;
        if (did_read <= 0)
            break;
        tty_in_buf_used += did_read;
    }

    return result;
}


void tty_gets(char *s, int size)
{
    wchar_t *ws = alloca(size * sizeof *ws);
    char *end = s + size - 1;

    if (!fgetws(ws, size, stdin))
	return;

    for (; *ws; ++ws) {
        char buf[UTF8_MAX];
        int n = utf8_put(*ws, buf);
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
