/*
 *  utils.c  --  miscellaneous utility functions
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
#include <errno.h>
#include <ctype.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>

#include "defines.h"
#include "main.h"
#include "utils.h"
#include "list.h"
#include "cmd.h"
#include "cmd2.h"
#include "beam.h"
#include "tty.h"
#include "edit.h"
#include "eval.h"
#include "log.h"

#define SAVEFILEVER 6

static char can_suspend = 0; /* 1 if shell has job control */

/*
 * GH: memory-"safe" strdup
 */
char *my_strdup __P1 (char *,s)
{
    if (s) {
	s = strdup(s);
	if (!s)
	    error = NO_MEM_ERROR;
    }
    return s;
}

/*
 * non-braindamaged strncpy:
 * copy up to len chars from src to dst, then add a final \0
 * (i.e. dst[len] = '\0')
 */
char *my_strncpy __P3 (char *,dst, char *,src, int,len)
{
    int slen = strlen(src);
    if (slen < len)
	return strcpy(dst, src);
    memcpy(dst, src, len);
    dst[len] = '\0';
    return dst;
}

/*
 * Determine the printed length of a string. This can be less than the string
 * length since it might contain escape sequences. Treated as such are
 * "<esc> [ <non-letters> <letter>", "<esc> <non-[>", "<control-char>".
 * This is not entirely universal but covers the most common cases (i.e. ansi)
 */
int printstrlen __P1 (char *,s)
{
    int l;
    enum { NORM, ESCAPE, BRACKET } state = NORM;
    for (l = 0; *s; s++) {
        switch(state) {
	 case NORM:
            if (*s == '\033') {
                state = ESCAPE;
            } else if ((*s & 0x80) || *s >= ' ') {
                l++;
            } else if (*s == '\r')
		l = (l / cols) * cols;
            break;
	    
	 case ESCAPE:
            state = (*s == '[') ? BRACKET : NORM;
            break;
	    
	 case BRACKET:
            if (isalpha(*s))
	      state = NORM;
            break;
        }
    }
    return l;
}

/*
 * return pointer to next non-blank char
 */
char *skipspace __P1 (char *,p)
{
    while (*p == ' ' || *p == '\t') p++;
    return p;
}

/*
 * find the first valid (non-escaped)
 * char in a string
 */
char *first_valid __P2 (char *,p, char,ch)
{
    if (*p && *p != ch) {
	p++;
	if (ch == ESC2 || ch == ESC) while (*p && *p != ch)
	    p++;
	else while (*p && ((*p != ch) || p[-1] == ESC))
	    p++;
    }
    return p;
}

/*
 * find the first regular (non-escaped, non in "" () or {} )
 * char in a string
 */
char *first_regular __P2 (char *,p, char,c)
{
    int escaped, quotes=0, paren=0, braces=0;

    while (*p && ((*p != c) || quotes || paren>0 || braces>0)) {
	escaped = 0;
	if (*p == ESC) {
	    while (*p == ESC)
		p++;
	    escaped = 1;
	}
	if (*p == ESC2) {
	    while (*p == ESC2)
		p++;
	    escaped = 0;
	}
	if (!*p)
	    break;
	if (!escaped) {
	    if (quotes) {
		if (*p == '\"')
		    quotes = 0;
	    }
	    else if (*p == '\"')
		quotes = 1;
	    else if (*p == ')')
		paren--;
	    else if (*p == '(')
		paren++;
	    else if (*p == '}')
		braces--;
	    else if (*p == '{')
		braces++;
	}
	p++;
    }
    return p;
}

/*
 * remove escapes (backslashes) from a string
 */
int memunescape __P2 (char *,p, int,lenp)
{
    char *start, *s;
    enum { NORM, ESCSINGLE, ESCAPE } state = NORM;

    if (!p || !*p)
	return 0;
    
    start = s = p;
    
    while (lenp) switch (state) {
      case NORM:
	if (*s != ESC) {
	    *p++ = *s++, lenp--;
	    break;
	}
	state = ESCSINGLE, s++;
	if (!--lenp)
	    break;
	/* fallthrough */
      case ESCSINGLE:
      case ESCAPE:
	if (*s == ESC)
	    state = ESCAPE, *p++ = *s++, lenp--;
	else if (*s == ESC2) 
	    state = NORM, *p++ = ESC, s++, lenp--;
	else {
	    if (state == ESCSINGLE && lenp >= 3 &&
		ISODIGIT(s[0]) && ISODIGIT(s[1]) && ISODIGIT(s[2])) {
		
		*p++ = ((s[0]-'0') << 6) | ((s[1]-'0') << 3) | (s[2]-'0');
		s += 3, lenp -= 3;
	    } else
		*p++ = *s++, lenp--;
	    state = NORM;
	}
	break;
      default:
	break;
    }
    *p = '\0';
    return (int)(p - start);
}

void unescape __P1 (char *,s)
{
    (void)memunescape(s, strlen(s));
}

void ptrunescape __P1 (ptr,p)
{
    if (!p)
	return;
    p->len = memunescape(ptrdata(p), ptrlen(p));
}

/*
 * add escapes (backslashes) to src
 */
ptr ptrmescape __P4 (ptr,dst, char *,src, int,srclen, int,append)
{
    int len;
    char *p;
    enum { NORM, ESCAPE } state;
    
    if (!src || srclen <= 0) {
	if (!append)
	    ptrzero(dst);
	return dst;
    }
    
    if (dst && append)
	len = ptrlen(dst);
    else
	len = 0;
    
    dst = ptrsetlen(dst, len + srclen*4); /* worst case */
    if (MEM_ERROR) return dst;
    
    dst->len = len;
    p = ptrdata(dst) + len;
    
    while (srclen) {
	state = NORM;
	if (*src == ESC) {
	    while (srclen && *src == ESC)
		dst->len++, *p++ = *src++, srclen--;
	    
	    if (!srclen || *src == ESC2)
		dst->len++, *p++ = ESC2;
	    else
		state = ESCAPE;
	}
	if (!srclen) break;
	
	if (*src < ' ' || *src > '~') {
	    sprintf(p, "\\%03o", (int)(byte)*src++);
	    len = strlen(p);
	    dst->len += len, p += len, srclen--;
	} else {
	    if (state == ESCAPE || strchr(SPECIAL_CHARS, *src))
		dst->len++, *p++ = ESC;
	
	    dst->len++, *p++ = *src++, srclen--;
	}
    }
    *p = '\0';
    return dst;
}

ptr ptrescape __P3 (ptr,dst, ptr,src, int,append)
{
    if (!src) {
	if (!append)
	    ptrzero(dst);
	return dst;
    }
    return ptrmescape(dst, ptrdata(src), ptrlen(src), append);
}

/*
 * add escapes to protect special characters from being escaped.
 */
void escape_specials __P2 (char *,dst, char *,src)
{
    enum { NORM, ESCAPE } state;
    while (*src) {
	state = NORM;
	if (*src == ESC) {
	    while (*src == ESC)
		*dst++ = *src++;
	    
	    if (!*src || *src == ESC2)
		*dst++ = ESC2;
	    else
		state = ESCAPE;
	}
	if (!*src) break;
	
	if (*src < ' ' || *src > '~') {
	    sprintf(dst, "\\%03o", (int)(byte)*src++);
	    dst += strlen(dst);
	} else {
	    if (state == ESCAPE || strchr(SPECIAL_CHARS, *src))
		*dst++ = ESC;
	
	    *dst++ = *src++;
	}
    }
    *dst = '\0';
}

/*
 * match mark containing & and $ and return 1 if matched, 0 if not
 * if 1, start and end contain the match bounds
 * if 0, start and end are undefined on return
 */
static int match_mark __P2 (marknode *,mp, char *,src)
{
    char *pat = mp->pattern;
    char *npat=0, *npat2=0, *nsrc=0, *prm=0, *endprm=0, *tmp, c;
    static char mpat[BUFSIZE];
    int mbeg = mp->mbeg, mword = 0, p;
    
    /* shortcut for #marks without wildcards */
    if (!mp->wild) {
	if ((nsrc = strstr(src, pat))) {
	    mp->start = nsrc;
	    mp->end = nsrc + strlen(pat);
	    return 1;
	}
	return 0;
    }

    mp->start = NULL;
    
    if (ISMARKWILDCARD(*pat))
	mbeg = - mbeg - 1;  /* pattern starts with '&' or '$' */

    while (pat && (c = *pat)) {
	if (ISMARKWILDCARD(c)) {
	    /* & matches a string */
	    /* $ matches a single word */
	    prm = src;
	    if (c == '$')
		mword = 1;
	    else if (!mp->start)
		mp->start = src;
	    ++pat;
	}
	
	npat  = first_valid(pat, '&');
	npat2 = first_valid(pat, '$');
	if (npat2 < npat) npat = npat2;
	if (!*npat) npat = 0;
	
	if (npat) {
	    my_strncpy(mpat, pat, npat-pat);
	    /* mpat[npat - pat] = 0; */
	} else
	    strcpy(mpat, pat);
	
	if (*mpat) {
	    nsrc = strstr(src, mpat);
	    if (!nsrc)
		return 0; 
	    if (mbeg > 0) {
		if (nsrc != src)
		    return 0;
		mbeg = 0;  /* reset mbeg to stop further start match */
	    }
	    endprm = nsrc;
	    if (!mp->start) {
		if (prm)
		    mp->start = src;
		else
		    mp->start = nsrc;
	    }
	    mp->end = nsrc + strlen(mpat);
	} else if (prm)           /* end of pattern space */
	    mp->end = endprm = prm + strlen(prm);
	else
	    mp->end = src;
	
	
	/* post-processing of param */
	if (mword) {
	    if (prm) {
		if (mbeg == -1) {
		    if (!*pat) {
			/* the pattern is "$", take first word */
			p = - 1;
		    } else {
			/* unanchored '$' start, take last word */
			tmp = memrchrs(prm, endprm - prm - 1, DELIM, DELIM_LEN);
			if (tmp)
			    p = tmp - prm;
			else
			    p = -1;
		    }
		    mp->start = prm += p + 1;
		} else if (!*pat) {
		    /* '$' at end of pattern, take first word */
		    if ((tmp = memchrs(prm, strlen(prm), DELIM, DELIM_LEN)))
			mp->end = endprm = tmp;
		} else {
			/* match only if param is single-worded */
		    if (memchrs(prm, endprm - prm, DELIM, DELIM_LEN))
			return 0;
		}
	    } else
		return 0;
	}
	if (prm)
	    mbeg = mword = 0;  /* reset match flags */
	src = nsrc + strlen(mpat);
	pat = npat;
    }
    return 1;
}

/*
 * add marks to line. write in dst.
 */
ptr ptrmaddmarks __P3 (ptr,dst, char *,line, int,len)
{
    marknode *mp, *mfirst;
    char begin[CAPLEN], end[CAPLEN], *lineend = line + len;
    int start = 1, matchlen, matched = 0;
    
    ptrzero(dst);
    
    if (!line || len <= 0)
	return dst;

    for (mp = markers; mp; mp = mp->next)
	mp->start = NULL;

    do {
	mfirst = NULL;
	for (mp = markers; mp; mp = mp->next) {
	    if (mp->start && mp->start >= line)
		matched = 1;
	    else {
		if (!(matched = (!mp->mbeg || start) && match_mark(mp, line)))
		    mp->start = lineend;
	    }
	    if (matched && mp->start < lineend &&
		(!mfirst || mp->start < mfirst->start))
		mfirst = mp;
	}
	
	if (mfirst) {
	    start = 0;
	    attr_string(mfirst->attrcode, begin, end);
	    
	    dst = ptrmcat(dst, line, matchlen = mfirst->start - line);
	    if (MEM_ERROR) break;
	    line += matchlen;
	    len -= matchlen;
	    
	    dst = ptrmcat(dst, begin, strlen(begin));
	    if (MEM_ERROR) break;

	    dst = ptrmcat(dst, line, matchlen = mfirst->end - mfirst->start);
	    if (MEM_ERROR) break;
	    line += matchlen;
	    len -= matchlen;
	    
	    dst = ptrmcat(dst, end, strlen(end));
	    if (MEM_ERROR) break;
	}
    } while (mfirst);

    if (!MEM_ERROR)
	dst = ptrmcat(dst, line, len);	
        
    return dst;
}

ptr ptraddmarks __P2 (ptr,dst, ptr,line)
{
    if (line)
	return ptrmaddmarks(dst, ptrdata(line), ptrlen(line));
    ptrzero(dst);
    return dst;
}

/*
 * write string to tty, wrapping to next line if needed.
 * don't print a final \n
 */
static void wrap_print __P1 (char *,s)
{
    char *p, c, follow = 1;
    char buf[BUFSIZE]; /* ASSERT(cols<BUFSIZE) */
    int l, m;
    enum { NORM, ESCAPE, BRACKET } state;
#ifdef BUG_ANSI
    int ansibug = 0;
#endif
    
    l = printstrlen(s);
#ifdef BUG_ANSI
    if (l > cols_1 && l < (int)strlen(s))
	ansibug = 1;
#endif
    
    while (l >= cols_1 - col0) {
        p = buf; m = 0; state = NORM;

	while (m < cols_1 - col0 && *s && *s != '\n') {
	    *p++ = c = *s++;
	    switch (state) {
	    case NORM:
		if (c == '\033')
		    state = ESCAPE;
		else if ((c & 0x80) || (c >= ' ' && c <= '~'))
		    m++, l--;
		else if (c == '\r')
		    m = 0;  
		break;
	    case ESCAPE:
		state = (c == '[') ? BRACKET : NORM;
		break;
	    case BRACKET:
		if (isalpha(c))
		    state = NORM;
		break;
	    }
	}
	
	follow = *s;

	*p = '\0';
	tty_printf("%s%s", buf, follow ? "\n" : "");
	if (follow)
	    col0 = 0;
    }
#ifdef BUG_ANSI
    if (ansibug)
	tty_printf("%s%s%s", follow ? s : "" ,
		   tty_modenorm, tty_clreoln);
    else	
#endif
	if (follow)
	    tty_puts(s);
}

/*
 * add marks to line and print.
 * if newline, also print a final \n
 */
void smart_print __P2 (char *,line, char,newline)
{
    static ptr ptrbuf = NULL;
    static char *buf;
    
    do {
	if (!ptrbuf) {
	    ptrbuf = ptrnew(PARAMLEN);
	    if (MEM_ERROR) break;
	}
	ptrbuf = ptrmaddmarks(ptrbuf, line, strlen(line));
	if (MEM_ERROR || !ptrbuf) break;

	buf = ptrdata(ptrbuf);
    
	if (opt_wrap)
	    wrap_print(buf);
	else {
#ifdef BUG_ANSI
	    int l;
	    l = printstrlen(buf);
	    if (l > cols_1 && l < ptrlen(ptrbuf))
		tty_printf("%s%s%s", buf, tty_modenorm, tty_clreoln);
	    else
#endif
		tty_printf("%s", buf);
	}
    } while(0);

    if (MEM_ERROR)
	print_error(error);
    else if (newline)
	col0 = 0, tty_putc('\n');
}

/*
 * copy first word of src into dst, and return pointer to second word of src
 */
char *split_first_word __P3 (char *,dst, int,dstlen, char *,src)
{
    char *tmp;
    int len;
    
    src = skipspace(src);
    if (!*src) {
	*dst='\0';
	return src;
    }
    len = strlen(src);
    
    tmp = memchrs(src, len, DELIM, DELIM_LEN);
    if (tmp) {
	if (dstlen > tmp-src+1) dstlen = tmp-src+1;
	my_strncpy(dst, src, dstlen-1);
    } else {
	my_strncpy(dst, src, dstlen-1);
	tmp = src + len;
    }
    if (*tmp && *tmp != CMDSEP) tmp++;
    return tmp;
}

static void sig_pipe_handler __P1 (int,signum)
{
    tty_puts("\n#broken pipe.\n");
}

static void sig_winch_handler __P1 (int,signum)
{
    sig_pending = sig_winch_got = 1;
}

static void sig_chld_handler __P1 (int,signum)
{
    sig_pending = sig_chld_got = 1;
}

static void sig_term_handler __P1 (int,signum)
{
    tty_printf("%s\n#termination signal.\n", edattrend);
    exit_powwow();
}

static void sig_intr_handler __P1 (int,signum)
{
    if (confirm) {
	tty_printf("%s\n#interrupted.%s\n", edattrend, tty_clreoln);
	exit_powwow();
    }
    
    PRINTF("%s\n#interrupted. Press again to quit%s\n", edattrend, tty_clreoln);
    tty_flush(); /* in case we are not in mainlupe */
    confirm = 1;
    error = USER_BREAK;
    
    sig_oneshot(SIGINT, sig_intr_handler);
}

/*
 * suspend ourselves
 */
void suspend_powwow __P1 (int, signum)
{
    if (can_suspend) {
        sig_permanent(SIGTSTP, SIG_DFL);
        sig_permanent(SIGTERM, SIG_IGN);
        sig_permanent(SIGINT, SIG_IGN);
	tty_puts(edattrend);
        tty_quit();
	
	if (kill(0, SIGTSTP) < 0) {
	    errmsg("suspend powwow");
	    return;
	}
	
        signal_start();
        tty_start();
        tty_sig_winch_bottomhalf();  /* in case size changed meanwhile */
    } else
	tty_puts("\n#I don't think your shell has job control.\n");
    status(1);
}

/*
 * GH: Sets a signal-handler permanently (like bsd signal())
 *     from Ilie
 */
function_signal sig_permanent __P2 (int,signum, function_signal,sighandler)
{
    struct sigaction act;
    function_signal oldhandler;
    
    if (sigaction(signum, NULL, &act))
        return SIG_ERR;
    oldhandler = act.sa_handler;
    act.sa_handler = sighandler;
#ifdef SA_RESTART
    act.sa_flags = SA_RESTART;
#else
    act.sa_flags = 0;
#endif
    if (sigaction(signum, &act, NULL))
	return SIG_ERR;
    return oldhandler;
}

/*
 * One-shot only signal. Hope it will work as intended.
 */
#ifdef SA_ONESHOT
function_signal sig_oneshot __P2 (int,signum, function_signal,sighandler)
{
    struct sigaction act;
    function_signal oldhandler;
    
    if (sigaction(signum, NULL, &act))
        return SIG_ERR;
    oldhandler = act.sa_handler;
    act.sa_handler = sighandler;
    act.sa_flags = SA_ONESHOT;
    if (sigaction(signum, &act, NULL))
	return SIG_ERR;
    return oldhandler;
}
#endif


/*
 * set up our signal handlers
 */
void signal_start __P0 (void)
{
    if (sig_permanent(SIGTSTP, SIG_IGN) == SIG_DFL) {
	sig_permanent(SIGTSTP, suspend_powwow);
	can_suspend = 1;
    }
    sig_permanent(SIGCHLD,  sig_chld_handler);
    sig_permanent(SIGQUIT,  sig_intr_handler);
    sig_permanent(SIGTERM,  sig_term_handler);
    sig_permanent(SIGPIPE,  sig_pipe_handler);
    sig_permanent(SIGWINCH, sig_winch_handler);
    /*
     * this must not be permanent, as we want
     * to be able to interrupt system calls
     */
    sig_oneshot(SIGINT, sig_intr_handler);
}

void sig_bottomhalf __P0 (void)
{
    if (sig_chld_got)
	sig_chld_bottomhalf();
    if (sig_winch_got)
	tty_sig_winch_bottomhalf();
    
    sig_pending = sig_chld_got = sig_winch_got = 0;
}

void errmsg __P1 (char *,msg)
{
    if (!msg)
	msg = "";
    
    clear_input_line(opt_compact);
    if (!opt_compact) {
	tty_putc('\n');
	status(1);
    }
    if (errno == EINTR) {
	tty_printf("#user break: %s (%d: %s)\n",
		  msg, errno, strerror(errno));
    } else if (errno) {
	tty_printf("#system call error: %s (%d", msg, errno);
	if (errno > 0)
	    tty_printf(": %s)\n", strerror(errno));
	else
	    tty_puts(")\n");
    } else if (error == NO_MEM_ERROR) {
	tty_printf("#system call error: %s (%d", msg, ENOMEM);
	tty_printf(": %s)\n", strerror(ENOMEM));
    }
    tty_flush();
}

/*
 * print system call error message and terminate
 */
void syserr __P1 (char *,msg)
{
    if (msg && *msg) {
	clear_input_line(opt_compact);
	if (!opt_compact) {
	    tty_putc('\n');
	    /* status(1); */
	}
	tty_flush();
	
	fprintf(stderr, "#powwow: fatal system call error:\n\t%s (%d", msg, errno);
	if (errno > 0)
	  fprintf(stderr, ": %s", strerror(errno));
	fprintf(stderr, ")\n");
    }
    
#ifdef SAVE_ON_SYSERR
    /* Try to do an emergency save. This can wreak havoc
     * if called from the wrong place, like
     * read_settings() or save_settings(),
     * thus is executed only if you add -DSAVE_ON_SYSERR
     * to CF flags in make_it
     */
    (void)save_settings();
#else
    tty_puts("#settings NOT saved to file.\n");
#endif
    
    tty_quit();
    exit(1);
}

static void load_missing_stuff __P1 (int,n)
{
    char buf[BUFSIZE];
    
    if (n < 1) {
        tty_add_walk_binds();
        tty_puts("#default keypad settings loaded\n");
    }
    if (n < 2) {
        tty_add_initial_binds();
        tty_puts("#default editing keys settings loaded\n");
    }
    if (n < 5) {
	static char *str[] = { "compact", "debug", "echo", "info",
		"keyecho", "speedwalk", "wrap", 0 };
	int i;
	for (i=0; str[i]; i++) {
	    sprintf(buf, "#%s={#if ($(1)==\"on\") #option +%s; #else #if ($(1)==\"off\") #option -%s; #else #option %s}",
		    str[i], str[i], str[i], str[i]);
	    parse_alias(buf);
	}
	tty_printf("#compatibility aliases loaded:\n\t%s\n",
		 "#compact, #debug, #echo, #info, #keyecho, #speedwalk, #wrap");
    }
    if (n < 6) {
	sprintf(buf, "#lines=#setvar lines=$0");
	parse_alias(buf);
	sprintf(buf, "#settimer=#setvar timer=$0");
	parse_alias(buf);
	limit_mem = 1048576;
	tty_printf("#compatibility aliases loaded:\n\t%s\n", "#lines, #settimer");
	tty_puts("#max text/strings length set to 1048576 bytes\n\tuse \"#setvar mem\" to change it\n\n#wait...");
	tty_flush();
	sleep(1);
	tty_puts("ok\n");
    }
}

/*
 * read definitions from file
 * return > 0 if success, < 0 if fail.
 * NEVER call syserr() from here or powwow will
 * try to save the definition file even if it got
 * a broken or empty one.
 */
int read_settings __P0 (void)
{
    FILE *f;
    char *buf, *p, *cmd, old_nice = a_nice;
    int failed = 1, n, savefilever = 0, left, len, limit_mem_hit = 0;
    varnode **first;
    ptr ptrbuf;
    
    if (!*deffile) {
	PRINTF("#warning: no save-file defined!\n");
	return 0;
    }
    
    f = fopen(deffile, "r");
    if (!f) {
        PRINTF("#error: cannot open file \"%s\": %s\n", deffile, strerror(errno));
	return 0;
    }

    ptrbuf = ptrnew(PARAMLEN);
    if (MEM_ERROR) {
	print_error(error);
	return 0;
    }
    buf = ptrdata(ptrbuf);
    left = ptrmax(ptrbuf);
    len = 0;
    
    opt_info = a_nice = 0;
    
    for (n = 0; n < MAX_HASH; n++) {
	while (aliases[n])
	    delete_aliasnode(&aliases[n]);
    }
    while (actions)
	delete_actionnode(&actions);
    while (prompts)
	delete_promptnode(&prompts);
    while (markers)
	delete_marknode(&markers);
    while (keydefs)
	delete_keynode(&keydefs);
    for (n = 0; n < MAX_HASH; n++) {
	while (named_vars[0][n])
	    delete_varnode(&named_vars[0][n], 0);
	first = &named_vars[1][n];
	while (*first) {
	    if (is_permanent_variable(*first))
		first = &(*first)->next;
	    else	
		delete_varnode(first, 1);
	}
    }
    
    for (n = 0; n < NUMVAR; n++) {
	*var[n].num = 0;
	ptrdel(*var[n].str);
	*var[n].str = NULL;
    }
    
    while (left > 0 && fgets(buf+len, left+1, f)) {
	/* WARNING: accessing private field ->len */
	len += n = strlen(buf+len);
	ptrbuf->len = len;
	left -= n;

	/* Clear all \n prefixed with a literal backslash '\\' */ 
	while( cmd = strstr( buf, "\\\n" ) ) {
		cmd[ 0 ] = ' ';
		cmd[ 1 ] = ' ';
	}

	cmd = strchr(buf, '\n');
	
	if (!cmd) {
	    if (feof(f)) {
		PRINTF("#error: missing newline at end of file \"%s\"\n", deffile);
		break;
	    }
	    /* no newline yet. increase line size and try again */
	    ptrbuf = ptrpad(ptrbuf, ptrlen(ptrbuf) >> 1);
	    if (MEM_ERROR) {
		limit_mem_hit = 1;
		print_error(error);
		break;
	    }
	    ptrtrunc(ptrbuf,len);
	    buf = ptrdata(ptrbuf);
	    left = ptrmax(ptrbuf) - len;
	    continue;
	}
	/* got a full line */
	*cmd = '\0';
	cmd = buf;
	left += len;
	len = 0;
	
        cmd = skipspace(cmd);
	if (!*cmd)
	    continue;
	
        error = 0;
        if (*(p = first_regular(cmd, ' '))) {
            *p++ = '\0';
            if (!strcmp(cmd, "#savefile-version")) {
		savefilever = atoi(p);
		continue;
            }
	    *--p = ' ';
	}
	parse_user_input(cmd, 1);
    }
    
    if (error)
	failed = -1;
    else if (ferror(f) && !feof(f)) {
	PRINTF("#error: cannot read file \"%s\": %s\n", deffile, strerror(errno));
        failed = -1;
    } else if (limit_mem_hit) {
	PRINTF("#error: cannot load save-file: got a line longer than limit\n");
	failed = -1;
    } else if (savefilever > SAVEFILEVER) {
	PRINTF("#warning: this powwow version is too old!\n");
    } else if (savefilever < SAVEFILEVER) {
	PRINTF("\n#warning: config file is from an older version\n");
	load_missing_stuff(savefilever);
    }
    
    fclose(f);
    a_nice = old_nice;
    
    if (ptrbuf)
	ptrdel(ptrbuf);
    
    return failed;
}

static char tmpname[BUFSIZE];

static void fail_msg __P0 (void)
{
    PRINTF("#error: cannot write to temporary file \"%s\": %s\n", tmpname, strerror(errno));
}

/*
 * save settings in definition file
 * return > 0 if success, < 0 if fail.
 * NEVER call syserr() from here or powwow will
 * enter an infinite loop!
 */
int save_settings __P0 (void)
{
    FILE *f;
    int l;
    aliasnode *alp;
    actionnode *acp;
    promptnode *ptp;
    marknode *mp;
    keynode *kp;
    varnode *vp;
    ptr pp = (ptr)0;
    extern char *full_options_string;
    int i, flag, failed = 1;

    if (REAL_ERROR) {
	PRINTF("#will not save after an error!\n");
	return -1;
    }
    error = 0;
    
    if (!*deffile) {
	PRINTF("#warning: no save-file defined!\n");
	return -1;
    }

    /*
     * Create a temporary file in the same directory as deffile,
     * and write settings there
     */
    strcpy(tmpname, deffile);
    l = strlen(tmpname) - 1;
    while (l && tmpname[l] != '/')
	l--;
    if (l)
	l++;
    
    sprintf(tmpname + l, "tmpsav%d%d", getpid(), rand() >> 8);
    if (!(f = fopen(tmpname, "w"))) {
        fail_msg();
        return -1;
    }
    
    pp = ptrnew(PARAMLEN);
    if (MEM_ERROR) failed = -1;

    failed = fprintf(f, "#savefile-version %d\n", SAVEFILEVER);
    if (failed > 0 && *hostname)
	failed = fprintf(f, "#host %s %d\n", hostname, portnumber);
    
    if (failed > 0) {
	if (delim_mode == DELIM_CUSTOM) {
	    pp = ptrmescape(pp, DELIM, strlen(DELIM), 0);
	    if (MEM_ERROR) failed = -1;
	}
	if (failed > 0)
	    failed = fprintf(f, "#delim %s%s\n", delim_name[delim_mode],
			     delim_mode == DELIM_CUSTOM ? ptrdata(pp) : "" );
    }

	if (failed > 0)
	    failed = fprintf(f, "#groupdelim %s\n", group_delim);

    if (failed > 0 && *initstr)
	failed = fprintf(f, "#init =%s\n", initstr);
    
    if (failed > 0 && limit_mem)
	failed = fprintf(f, "#setvar mem=%d\n", limit_mem);

    if (failed > 0 && (i = log_getsize()))
	failed = fprintf(f, "#setvar buffer=%d\n", i);

    if (failed > 0) {
	reverse_sortedlist((sortednode **)&sortedaliases);
	for (alp = sortedaliases; alp && failed > 0; alp = alp->snext) {
	    pp = ptrmescape(pp, alp->name, strlen(alp->name), 0);
	    if (MEM_ERROR) { failed = -1; break; }
	    failed = fprintf(f, "#alias %s%s%s=%s\n", ptrdata(pp),
		alp -> group == NULL ? "" : group_delim,
		alp -> group == NULL ? "" : alp -> group, 
		alp->subst);
	}
	reverse_sortedlist((sortednode **)&sortedaliases);
    }
    
    for (acp = actions; acp && failed > 0; acp = acp->next) {
	failed = fprintf(f, "#action %c%c%s%s%s %s=%s\n",
			 action_chars[acp->type], acp->active ? '+' : '-',
			 acp->label, 
		acp -> group == NULL ? "" : group_delim,
		acp -> group == NULL ? "" : acp -> group, 
			 acp->pattern, acp->command);
    }

    for (ptp = prompts; ptp && failed > 0; ptp = ptp->next) {
	failed = fprintf(f, "#prompt %c%c%s %s=%s\n",
			 action_chars[ptp->type], ptp->active ? '+' : '-',
			 ptp->label, ptp->pattern, ptp->command);
    }
    
    for (mp = markers; mp && failed > 0; mp = mp->next) {
	pp = ptrmescape(pp, mp->pattern, strlen(mp->pattern), 0);
	if (MEM_ERROR) { failed = -1; break; }
	failed = fprintf(f, "#mark %s%s=%s\n", mp->mbeg ? "^" : "",
			     ptrdata(pp), attr_name(mp->attrcode));
    }
    /* save value of global variables */
    
    for (flag = 0, i=0; i<NUMVAR && failed > 0; i++) {
        if (var[i].num && *var[i].num) {    /* first check was missing!!! */
	    failed = fprintf(f, "%s@%d = %ld", flag ? ", " : "#(",
			     i-NUMVAR, *var[i].num);
            flag = 1;
        }
    }
    if (failed > 0 && flag) failed = fprintf(f, ")\n");
    
    for (i=0; i<NUMVAR && failed > 0; i++) {
        if (var[i].str && *var[i].str && ptrlen(*var[i].str)) {
	    pp = ptrescape(pp, *var[i].str, 0);
	    if (MEM_ERROR) { failed = -1; break; }
	    failed = fprintf(f, "#($%d = \"%s\")\n", i-NUMVAR, ptrdata(pp));
	}
    }
    
    if (failed > 0) {
	reverse_sortedlist((sortednode **)&sortednamed_vars[0]);
	for (flag = 0, vp = sortednamed_vars[0]; vp && failed > 0; vp = vp->snext) {
	    if (vp->num) {
		failed = fprintf(f, "%s@%s = %ld", flag ? ", " : "#(",
				 vp->name, vp->num);
		flag = 1;
	    }
	}
	reverse_sortedlist((sortednode **)&sortednamed_vars[0]);
    }
    if (failed > 0 && flag) failed = fprintf(f, ")\n");
    
    if (failed > 0) {
	reverse_sortedlist((sortednode **)&sortednamed_vars[1]);
	for (vp = sortednamed_vars[1]; vp && failed > 0; vp = vp->snext) {
	    if (!is_permanent_variable(vp) && vp->str && ptrlen(vp->str)) {
		pp = ptrescape(pp, vp->str, 0);
		if (MEM_ERROR) { failed = -1; break; }
		failed = fprintf(f, "#($%s = \"%s\")\n", vp->name, ptrdata(pp));
	    }
	}
	reverse_sortedlist((sortednode **)&sortednamed_vars[1]);
    }
    
    /* GH: fixed the history and word completions saves */
    if (failed > 0 && opt_history) {
	l = (curline + 1) % MAX_HIST;
	while (failed > 0 && l != curline) {
	    if (hist[l] && *hist[l]) {
		pp = ptrmescape(pp, hist[l], strlen(hist[l]), 0);
		if (MEM_ERROR) { failed = -1; break; }
		failed = fprintf(f, "#put %s\n", ptrdata(pp));
	    }
	    if (++l >= MAX_HIST)
		l = 0;
	}
    }
    
    if (failed > 0 && opt_words) {
	int cl = 4, len;
	l = wordindex;
	flag = 0;
	while (words[l = words[l].next].word)
	    ;
	while (words[l = words[l].prev].word && failed > 0) {
	    if (~words[l].flags & WORD_RETAIN) {
		pp = ptrmescape(pp, words[l].word, strlen(words[l].word), 0);
		len = ptrlen(pp) + 1;
		if (cl > 4 && cl + len >= 80) {
		    cl = 4;
		    failed = fprintf(f, "\n");
		    flag = 0;
		}
		if (failed > 0)
		    failed = fprintf(f, "%s %s", flag ? "" : "#add", ptrdata(pp));
		cl += len;
		flag = 1;
	    }
	}
	if (failed > 0 && flag)
	    failed = fprintf(f, "\n");
    }
    
    for (kp = keydefs; kp && failed > 0; kp = kp->next) {
	if (kp->funct==key_run_command)
	    failed = fprintf(f, "#bind %s %s=%s\n", kp->name, 
			     seq_name(kp->sequence, kp->seqlen), kp->call_data);
	else
	    failed = fprintf(f, "#bind %s %s=%s%s%s\n", kp->name,
			     seq_name(kp->sequence, kp->seqlen),
			     internal_functions[lookup_edit_function(kp->funct)].name,
			     kp->call_data ? " " : "",
			     kp->call_data ? kp->call_data : "");
    }
    
    if (failed > 0)
	failed =
	fprintf(f, full_options_string,
		opt_exit    ? '+' : '-',
		opt_history ? '+' : '-',
		opt_words   ? '+' : '-',
		opt_compact ? '+' : '-',
		opt_debug   ? '+' : '-',
		opt_echo    ? '+' : '-',
		opt_info    ? '+' : '-',
		opt_keyecho    ? '+' : '-',
		opt_speedwalk ? '+' : '-',
		opt_wrap      ? '+' : '-',
		opt_autoprint ? '+' : '-',
		opt_reprint   ? '+' : '-',
		opt_sendsize  ? '+' : '-',
		opt_autoclear ? '+' : '-',
		"\n");

    fclose(f);
    
    if (error)
	errmsg("malloc");
    else if (failed <= 0)
	fail_msg();
    else {
	failed = rename(tmpname, deffile);
	if (failed == -1) {
	    PRINTF("#error: cannot move temporary file \"%s\" to \"%s\": %s\n",
		       tmpname, deffile, strerror(errno));
	} else
	    failed = 1;
    }
    
    if (pp)
	ptrdel(pp);
    
    return failed > 0 ? 1 : -1;
}

/*
 * update "now" to current time
 */
void update_now __P0 (void)
{
    if (!now_updated) {
	gettimeofday(&now, NULL);
	now_updated = 1;
    }
}

/*
 * terminate powwow as cleanly as possible
 */
void exit_powwow __P0 (void)
{
    log_flush();
    if (capturefile) fclose(capturefile);
    if (recordfile)  fclose(recordfile);
    if (moviefile)   fclose(moviefile);
    (void)save_settings();
    show_stat();
    tty_quit();
    exit(0);
}
