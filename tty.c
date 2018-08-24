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

#ifdef USE_LOCALE
#include <wchar.h>
#include <locale.h>
#endif

#include "defines.h"
#include "main.h"
#include "edit.h"
#include "utils.h"
#include "list.h"
#include "tty.h"
#include "tcp.h"

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

#ifdef BSD_LIKE
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

/* int ioctl(); */

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

int tgetent();
int tgetnum();
int tgetflag();
char *tgetstr();
char *tgoto();

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

static int extract __P ((char *cap, char *buf));

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
} tty_write_state;
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
void tty_start __P0 (void)
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
    tty_write_state.fd = 1;
    wcrtomb(NULL, L'\0', &tty_write_state.mbstate);
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
void tty_quit __P0 (void)
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
void tty_special_keys __P0 (void)
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
void tty_sig_winch_bottomhalf __P0 (void)
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
	
	if (tcp_main_fd != -1)
	    tcp_write_tty_size();
	line0 += lines - olines;
	
	tty_gotoxy(0, line0);
	/* so we know where the cursor is */
#ifdef BUG_ANSI
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
void tty_bootstrap __P0 (void)
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
void tty_add_walk_binds __P0 (void)
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
void tty_add_initial_binds __P0 (void)
{
    struct b_init_node {
	char *label, *seq;
	function_any funct;
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
	{ "C-z",	"\032",		suspend_powwow },
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
static int extract __P2 (char *,cap, char *,buf)
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
void tty_gotoxy __P2 (int,col, int,line)
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
void tty_gotoxy_opt __P4 (int,fromcol, int,fromline, int,tocol, int,toline)
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
	    /*
	     * if hiliting in effect or prompt contains escape sequences,
	     * just use tty_gotoxy
	     */
	    if (cost + i > gotocost || *edattrbeg || promptlen != col0)
		break;
	    if (fromcol < col0 && toline == line0) {
		strcpy(cp, promptstr+fromcol);
		cp += promptlen-fromcol;
		fromcol = col0;
	    }
	    my_strncpy(cp, edbuf + (toline - line0) * cols_1 + fromcol - col0,
		    tocol - fromcol);
	    cp += tocol - fromcol;
	}
	*cp = 0;
	tty_puts(buf);
	return;
    }
    tty_gotoxy(tocol, toline);
}


/*
 * GH: change the position on input line (gotoxy there, and set pos)
 *     from cancan 2.6.3a
 */
void input_moveto __P1 (int,new_pos)
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
    
    if (line_status == 0) {
	int fromline = CURLINE(pos), toline = CURLINE(new_pos);
	if (toline < 0)
	    line0 -= toline, toline = 0;
	else if (toline > lines - 1)
	    line0 -= toline - lines + 1, toline = lines - 1;
	tty_gotoxy_opt(CURCOL(pos), fromline, CURCOL(new_pos), toline);
    }
    pos = new_pos;
}

/*
 * delete n characters at current position (the position is unchanged)
 * assert(n < edlen - pos)
 */
void input_delete_nofollow_chars __P1 (int,n)
{
    int r_cost, p = pos, d_cost;
    int nl = pos - CURCOL(pos);	/* this line's starting pos (can be <= 0) */
    int cl = CURLINE(pos);	/* current line */
    
    if (n > edlen - p)
	n = edlen - p;
    if (n <= 0)
	return;

    d_cost = p + n;
    if (line_status != 0) {
	memmove(edbuf + p, edbuf + d_cost, edlen - d_cost + 1);
	edlen -= n;
	return;
    }
    
    memmove(edbuf + p, edbuf + d_cost, edlen - d_cost);
    memset(edbuf + edlen - n, (int)' ', n);
    for (;; tty_putc('\n'), p = nl, cl++) {
	d_cost = 0;
	/* FEATURE: ought to be "d_cost = n > gotocost ? -gotocost : -n;"
	 * since redraw will need to goto back. Of little importance */
	if ((r_cost = edlen) > (nl += cols_1))
	    r_cost = nl, d_cost = n + gotocost;
	r_cost -= p;
#ifndef USE_VT100
	/*
	 * FEATURE: no clreoln is used (it might cost less in the occasion
	 * we delete more than one char). Simplicity
	 */
	if (deletecost && deletecost * n + d_cost < r_cost) {
#ifdef BUG_ANSI
	    if (edattrbg)
		tty_puts(edattrend);
#endif	    
	    for (d_cost = n; d_cost; d_cost--)
	        tty_puts(delchar);
#ifdef BUG_ANSI
	    if (edattrbg)
		tty_puts(edattrbeg);
#endif	    

	    if (edlen <= nl)
		break;
	    
	    tty_gotoxy(cols_1 - n, cl);
	    tty_printf("%.*s", n, edbuf + nl - n);
	} else
#endif /* not USE_VT100 */
	{
#ifdef BUG_ANSI
	    if (edattrbg && p <= edlen - n && p + r_cost >= edlen - n)
		tty_printf("%.*s%s%.*s", edlen - p - n, edbuf + p,
					 edattrend,
					 r_cost - edlen + p + n, edbuf + edlen - n);
	    else
#endif
		tty_printf("%.*s", r_cost, edbuf + p);
	    
	    p += r_cost;
	    
	    if (edlen <= nl) {
#ifdef BUG_ANSI
		if (edattrbg)
		    tty_puts(edattrbeg);
#endif
		break;
	    }
	}
    }
    edbuf[edlen -= n] = '\0';
    switch(pos - p) {
      case 1:
	tty_puts(leftcur);
	break;
      case 0:
	break;
      default:
	tty_gotoxy_opt(CURCOL(p), cl, CURCOL(pos), CURLINE(pos));
	break;
    }
}

/*
 * GH: print a char on current position (overwrite), advance position
 *     from cancan 2.6.3a
 */
void input_overtype_follow __P1 (char,c)
{
    if (pos >= edlen)
	return;
    edbuf[pos++] = c;

    if (line_status == 0) {
	tty_putc(c);
	if (!CURCOL(pos)) {
#ifdef BUG_ANSI
	    if (edattrbg)
		tty_printf("%s\n%s", edattrend, edattrbeg);
	    else
#endif
		tty_putc('\n');
	}
    }
}

/*
 * insert n characters at input line current position.
 * The position is set to after the inserted characters.
 */
void input_insert_follow_chars __P2 (char *,str, int,n)
{
    int r_cost, i_cost, p = pos;
    int nl = p - CURCOL(p);	/* next line's starting pos */
    int cl = CURLINE(p);	/* current line */

    if (edlen + n >= BUFSIZE)
	n = BUFSIZE - edlen - 1;
    if (n <= 0)
	return;
    
    memmove(edbuf + p + n, edbuf + p, edlen + 1 - p);
    memmove(edbuf + p, str, n);
    edlen += n; pos += n;
    
    if (line_status != 0)
	return;
    
    do {
	i_cost = n;
	if ((r_cost = edlen) > (nl += cols_1))
	    r_cost = nl, i_cost += insertfinish;
	r_cost -= p;
#ifndef USE_VT100
	/* FEATURE: insert mode is used only when one char is inserted
	 (which is probably true > 95% of the time). Simplicity */
	if (n == 1 && inscharcost && inscharcost + i_cost < r_cost) {
	    tty_printf("%s%c", inschar, edbuf[p++]);
	    if (edlen > nl && !wrapglitch) {
		tty_gotoxy(cols_1, cl);
		tty_puts(clreoln);
	    }
	} else
#endif /* not USE_VT100 */
	{
	    tty_printf("%.*s", r_cost, edbuf + p);  p += r_cost;
	}
	if (edlen < nl)
	    break;
#ifdef BUG_ANSI
	if (edattrbg)
	    tty_printf("%s\n%s", edattrend, edattrbeg);
	else
#endif
	    tty_puts("\n");
	p = nl;
	if (++cl > lines - 1) {
	    cl = lines - 1;
	    line0--;
	}
    } while (edlen > nl);
    
    if (p != pos)
	tty_gotoxy_opt(CURCOL(p), cl, CURCOL(pos), CURLINE(pos));
}

#ifdef USE_LOCALE
/* curses wide character support by Dain */

void tty_puts __P ((const char *s))
{
    while (*s)
        tty_putc(*s++);
}

void tty_putc __P ((char c))
{
    size_t r;
    int ignore_error = 0;
    if (tty_write_state.used + MB_LEN_MAX > sizeof tty_write_state.data)
        tty_flush();
again:
    r = wcrtomb(tty_write_state.data + tty_write_state.used, (unsigned char)c,
                &tty_write_state.mbstate);
    if (r == (size_t)-1) {
        if (ignore_error)
            return;
        if (errno != EILSEQ) {
            perror("mcrtomb()");
            abort();
        }
        /* character cannot be represented; try to write a question
         * mark instead, but ignore any errors */
        ignore_error = 1;
        c = '?';
        goto again;
    }
    assert(r <= MB_LEN_MAX);
    tty_write_state.used += r;
}

int tty_printf __P ((const char *format, ...))
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
static int tty_in_buf_used = 0;

int tty_has_chars __P ((void))
{
    return !!tty_in_buf_used;
}

void bad_utf8(wchar_t * pwc, size_t bytes, unsigned codepoint)
{
  if (pwc != NULL)
    *pwc = (wchar_t) '?';
  fprintf(stderr, "WARNING: Unrecognized %d-byte UTF-8 codepoint U+%04X.\n",
	  bytes, codepoint);
}

void reset_mbtowc()
{
  mbtowc(NULL, NULL, 0);
}

int pow_mbtowc(wchar_t * pwc, const char *s, size_t n)
{
  int ret = mbtowc(pwc, s, n);
  if (ret >= 0)
    return ret;

  // mbtowc can't parse it. Let's try UTF-8.
  if (n >= 1 && (s[0] & 0x80) == 0x00)
    {
      // 1-byte UFF-8: U+0000 U+007F
      // 0xxxxxxx
      if (pwc != NULL)
	{
	  unsigned codepoint = s[0] & 0x7f;
	  *pwc = (wchar_t) codepoint;
	}
      reset_mbtowc();
      return 1;
    }
  else if (n >= 2 && (s[0] & 0xe0) == 0xc0 && (s[1] & 0xc0) == 0x80)
    {
      // 2-byte UTF-8: U+0080 to U+07FF
      // 110xxxxx 10xxxxxx
      if (pwc != NULL)
	{
	  unsigned tmp0 = s[0] & 0x1f;
	  unsigned tmp1 = s[1] & 0x3f;
	  unsigned codepoint = (tmp0 << 6) | tmp1;
	  if (codepoint < 0x80 || codepoint > 0xFF)
	    bad_utf8(pwc, 2, codepoint);
	  else
	    *pwc = (wchar_t) codepoint;
	}
      reset_mbtowc();
      return 2;
    }
  else if (n >= 3 && (s[0] & 0xf0) == 0xe0 && (s[1] & 0xc0) == 0x80
	   && (s[2] & 0xc0) == 0x80)
    {
      // 3-byte UTF-8: U+0800 to U+FFFF:
      // 1110xxxx 10xxxxxx 10xxxxxx
      unsigned tmp0 = s[0] & 0x0f;
      unsigned tmp1 = s[1] & 0x3f;
      unsigned tmp2 = s[2] & 0x3f;
      unsigned codepoint = (tmp0 << 12) | (tmp1 << 6) | tmp2;
      bad_utf8(pwc, 3, codepoint);
      reset_mbtowc();
      return 3;
    }
  else if (n >= 4 && (s[0] & 0xf8) == 0xf0 && (s[1] & 0xc0) == 0x80
	   && (s[2] & 0xc0) == 0x80 && (s[3] & 0xc0) == 0x80)
    {
      // 4-byte UFF-8: U+10000 to U+10FFFF
      // 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx
      unsigned tmp0 = s[0] & 0x07;
      unsigned tmp1 = s[1] & 0x3f;
      unsigned tmp2 = s[2] & 0x3f;
      unsigned tmp3 = s[3] & 0x3f;
      unsigned codepoint = (tmp0 << 18) | (tmp1 << 12) | (tmp2 << 6) | tmp3;
      bad_utf8(pwc, 4, codepoint);
      reset_mbtowc();
      return 4;
    }
  else if (n >= MB_CUR_MAX)
    {
      if (pwc != NULL)
	*pwc = (wchar_t) '?';
      fprintf(stderr, "WARNING: Unrecognized %d-byte codepoint.\n",
	      MB_CUR_MAX);
      reset_mbtowc();
      return MB_CUR_MAX;
    }

  return ret;
}

int tty_read __P ((char *buf, size_t count))
{
    int result = 0;
    int converted;
    wchar_t wc;
    int did_read = 0, should_read = 0;
    
    if (count && tty_in_buf_used) {
	converted = pow_mbtowc(&wc, tty_in_buf, tty_in_buf_used);
	if (converted >= 0)
	    goto another;
    }

    while (count) {
	should_read = sizeof tty_in_buf - tty_in_buf_used;
	did_read = read(tty_read_fd, tty_in_buf + tty_in_buf_used,
                        should_read);

	if (did_read < 0 && errno == EINTR)
	    continue;
	if (did_read <= 0)
	    break;

	tty_in_buf_used += did_read;

	converted = pow_mbtowc(&wc, tty_in_buf, tty_in_buf_used);
	if (converted < 0)
	    break;

    another:
	if (converted == 0)
	    converted = 1;

	if (!(wc & ~0xff)) {
	    *buf++ = (unsigned char)wc;
	    --count;
	    ++result;
	}

	tty_in_buf_used -= converted;
	memmove(tty_in_buf, tty_in_buf + converted, tty_in_buf_used);

	if (count == 0)
	    break;

	converted = pow_mbtowc(&wc, tty_in_buf, tty_in_buf_used);
	if (converted >= 0 && tty_in_buf_used)
	    goto another;

	if (did_read < should_read)
	    break;
    }

    return result;
}


void tty_gets __P ((char *s, int size))
{
    wchar_t *ws = alloca(size * sizeof *ws);

    if (!fgetws(ws, size, stdin))
	return;

    while (*ws) {
	if (!(*ws & ~0xff))
	    *s++ = (unsigned char)*ws;
	++ws;
    }
}

void tty_flush __P ((void))
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

void tty_raw_write __P ((char *data, size_t len))
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
