/*
 *  edit.c  --  line editing functions for powwow
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
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <sys/types.h>
#include <sys/time.h>

#include "defines.h"
#include "main.h"
#include "utils.h"
#include "cmd.h"
#include "edit.h"
#include "tcp.h"
#include "tty.h"
#include "eval.h"
#include "log.h"

static void insert_string	__P ((char *arg));

/* history buffer */
char *hist[MAX_HIST];	/* saved history lines */
int curline = 0;	/* current history line */
int pickline = 0;	/* line to pick history from */

/* word completion list */
wordnode words[MAX_WORDS];
int wordindex = 0;

edit_function internal_functions[] = {
    {(char *)0, (function_str)0, },
    {"&enter-line", enter_line, },
    {"&complete-word", complete_word, },
    {"&complete-line", complete_line, },
    {"&del-char-left", del_char_left, },
    {"&del-char-right",  del_char_right, },
    {"&prev-char", prev_char, },
    {"&prev-line", prev_line, },
    {"&next-char", next_char, },
    {"&next-line", next_line, },
    {"&to-history", to_history, },
    {"&clear-line", clear_line, },
    {"&redraw-line", redraw_line, },
    {"&redraw-line-noprompt", redraw_line_noprompt, },
    {"&begin-of-line", begin_of_line, },
    {"&end-of-line", end_of_line, },
    {"&kill-to-eol", kill_to_eol, },
    {"&transpose", transpose_chars, },
    {"&transpose-words", transpose_words, },
    {"&suspend", (function_str)suspend_powwow, }, /* yep, it's an hack */
    {"&del-word-left", del_word_left, },
    {"&del-word-right", del_word_right, },
    {"&prev-word", prev_word, },
    {"&upcase-word", upcase_word, },
    {"&downcase-word", downcase_word, },
    {"&next-word", next_word, },
    {"&insert-string", insert_string, },
    {(char *)0, (function_str)0 }
};

int lookup_edit_name __P2 (char *,name, char **,arg)
{
    int i, len, flen;
    char *fname, *extra = NULL;
    
    if ((fname = strchr(name, ' ')))
	len = fname - name;
    else
	len = strlen(name);
    
    for (i=1; (fname = internal_functions[i].name); i++) {
	flen = strlen(fname);
	if (flen == len && !strncmp(name, fname, flen)) {
	    extra = name + flen;
	    if (*extra == ' ') extra++;
	    if (!*extra) extra = NULL;
	    *arg = extra;
	    return i;
	}
    }
    *arg = extra;
    return 0;
}

int lookup_edit_function __P1 (function_str,funct)
{
    int i;
    function_str ffunct;
    
    for (i = 1; (ffunct = internal_functions[i].funct); i++)
	if (funct == ffunct)
	    return i;
    
    return 0;
}

/*
 * redisplay the prompt
 * assume cursor is at beginning of line
 */
void draw_prompt __P0 (void)
{
    if (promptlen && prompt_status == 1) {
	int e = error;
	error = 0;
	marked_prompt = ptraddmarks(marked_prompt, prompt->str);
	if (MEM_ERROR) { promptzero(); errmsg("malloc(prompt)"); return; }
	tty_puts(ptrdata(marked_prompt));
	col0 = printstrlen(promptstr); /* same as printstrlen(marked_prompt) */
	error = e;
    }
    prompt_status = 0;
}

/*
 * clear current input line (deleteprompt == 1 if to clear also prompt)
 * cursor is left right after the prompt.
 * 
 * since we do not expect data from the user at this point,
 * do not print edattrbeg now.
 */
void clear_input_line __P1 (int,deleteprompt)
{
    /*
     * be careful: if prompt and/or input line have been erased from screen,
     * pos will be different from the actual cursor position
     */
    if ((edlen && line_status == 0) || (promptlen && prompt_status == 0 && deleteprompt)) {
	int newcol = deleteprompt ? 0 : col0;
	int realpos = line_status == 0 ? pos : (prompt_status == 0 ? 0 : -col0);
	
	tty_gotoxy_opt(CURCOL(realpos), CURLINE(realpos), newcol, line0);
	tty_puts(edattrend);
	if (line0 < lines - 1)
	    tty_puts(tty_clreoscr);
        else
	    tty_puts(tty_clreoln);
	col0 = newcol;
    } else {
	tty_puts(edattrend);
    }
    if (deleteprompt)
	status(1);
    else
	line_status = 1;
}

/*
 * clear input line, but do nothing else
 */
void clear_line __P1 (char *,dummy)
{
    if (!edlen)
	return;
    clear_input_line(0);
    pickline = curline;
    *edbuf = '\0';
    pos = edlen = 0;
}

/*
 * Redraw the input line and put the cursor at the current position.
 * The cursor is assumed to be directly after the prompt.
 */
void draw_input_line __P0 (void)
{
    int i, oldline0;
    
    if (line_status == 0 || linemode & LM_NOECHO)
	return;
    
    tty_puts(edattrbeg);
    
    if (edlen) {
	oldline0 = line0;
	if (edlen < cols_1 - col0) {
	    tty_puts(edbuf);
	} else {
	    tty_printf("%.*s", cols_1 - col0, edbuf);
	    for (i = cols_1 - col0; i <= edlen; i += cols_1) {
#ifdef BUG_ANSI
		if (edattrbg) 
		    tty_printf("%s\n%s%.*s", edattrend, edattrbeg, cols_1, edbuf + i);
		else
#endif
		    tty_printf("\n%.*s", cols_1, edbuf + i);
	    }
	}
	line0 = lines - (edlen + col0) / cols_1 - 1;
	if (line0 > oldline0)
	    line0 = oldline0;
	if ((i = CURLINE(pos)) < 0)
	    line0 -= i;
	else if (i > lines - 1)
	    line0 -= i - lines + 1;
	tty_gotoxy_opt(CURCOL(edlen), CURLINE(edlen), CURCOL(pos), CURLINE(pos));
    }
    line_status = 0;
}

/*
 * redraw the input line
 */
void redraw_line __P1 (char *,dummy)
{
    clear_input_line(1);
}

/*
 * redraw the input line, clearing the prompt
 */
void redraw_line_noprompt __P1 (char *,dummy)
{
    clear_input_line(0);
    tty_putc('\n');
    if (line0 < lines - 1)
	line0++;
    status(-1);
}

/*
 * GH: transpose two words to the left
 */
void transpose_words __P1 (char *,dummy)
{
    /* other refers to the word to the left, this is the one we are at */
    
    int this_so, other_so, this_eo, other_eo;
    char buf[BUFSIZE];
    int n;
    
    if (pos > 2) {
	
	this_eo = this_so = pos;
	/* optionally traceback to find a word */
	while (this_so && strchr(DELIM, edbuf[this_so]))
	    this_so--;
	
	/* now find where the current word ends */
	while (this_eo < edlen && !strchr(DELIM, edbuf[this_eo]))
	    this_eo++;
	
	/* found a word; now find its start */
	while (this_so > 0 && !strchr(DELIM, edbuf[this_so - 1]))
	    this_so--;
	
	if (this_so < 2)
	    return;		/* impossible that there's another word */
	
	other_so = this_so - 1;
	while (other_so >= 0 && strchr(DELIM, edbuf[other_so]))
	    other_so--;
	if (other_so < 0)
	    return;
	other_eo = other_so + 1;
	while (other_so > 0 && !strchr(DELIM, edbuf[other_so - 1]))
	    other_so--;
	
	sprintf(buf, "%.*s%.*s%.*s",
		this_eo - this_so, edbuf + this_so, 
		this_so - other_eo, edbuf + other_eo,
		other_eo - other_so, edbuf + other_so);
	
	input_moveto(other_so);
	for (n = 0; buf[n]; input_overtype_follow(buf[n++]))
	    ;
    }
}

/*
 * transpose two characters to the left
 */
void transpose_chars __P1 (char *,dummy)
{
    int i, j;
    char c;
    if (pos > 1 || (pos > 0 && pos < edlen)) {
        if (pos < edlen) {
	    j = pos;
	    i = pos - 1;
	} else {
	    j = pos - 1;
	    i = pos - 2;
	}
	c = edbuf[j]; edbuf[j] = edbuf[i]; edbuf[i] = c;
	
	if (line_status == 0) {
	    tty_gotoxy_opt(CURCOL(pos), CURLINE(pos), CURCOL(i), CURLINE(i));
	    tty_putc(edbuf[i]);
	    tty_gotoxy_opt(CURCOL(i+1), CURLINE(i+1), CURCOL(j), CURLINE(j));
	    tty_putc(edbuf[j]);
	    if (pos < edlen) {
		pos++;
		tty_gotoxy_opt(CURCOL(j+1), CURLINE(j+1), CURCOL(pos), CURLINE(pos));
	    }
	} else
	    pos++;
    }
}

/*
 * erase everything to the end of line
 */
void kill_to_eol __P1 (char *,dummy)
{
    if (line_status == 0) {
	if (edattrbg) 
	    tty_printf("%s%s", edattrend, tty_clreoln);
	else 
	    tty_puts(tty_clreoln);
	if (CURLINE(edlen) > CURLINE(pos)) {
	    tty_printf("\n%s", tty_clreoscr);
	    tty_gotoxy_opt(0, CURLINE(pos) + 1, CURCOL(pos), CURLINE(pos));
	}
	if (edattrbg)
	    tty_puts(edattrbeg);
    }
    edbuf[edlen = pos] = '\0';
}

/*
 * move cursor to end of line
 */
void end_of_line __P1 (char *,dummy)
{
    input_moveto(edlen);
}

/*
 * move cursor to beginning of line
 */
void begin_of_line __P1 (char *,dummy)
{
    input_moveto(0);
}

/*
 * delete a character to the right
 */
void del_char_right __P1 (char *,dummy)
{
    input_delete_nofollow_chars(1);
}

/*
 * delete a character to the left
 */
void del_char_left __P1 (char *,dummy)
{
    if (pos) {
	input_moveto(pos-1);
	input_delete_nofollow_chars(1);
    }
}

/*
 * move a line into history, but don't do anything else
 */
void to_history __P1 (char *,dummy)
{
    if (!edlen)
	return;
    clear_input_line(0);
    put_history(edbuf);
    pickline = curline;
    *edbuf = '\0';
    pos = edlen = 0;
}

/*
 * put string in history at current position
 * (string is assumed to be trashable)
 */
void put_history __P1 (char *,str)
{
    char *p;
    if (hist[curline]) free(hist[curline]);
    if (!(hist[curline] = my_strdup(str))) {
	errmsg("malloc");
	return;
    }
    
    if (++curline == MAX_HIST)
	curline = 0;
    
    /* split into words and put into completion list */
    for (p = strtok(str, DELIM); p; 
	 p = strtok(NULL, DELIM)) {
        if (strlen(p) >= MIN_WORDLEN && 
	    p[0] != '#') /* no commands/short words */
	    put_word(p);
    }
}

/*
 * move a node before wordindex, i.e. make it the last word
 */
static void demote_word __P1 (int,i)
{
    words[words[i].prev].next = words[i].next;
    words[words[i].next].prev = words[i].prev;
    words[i].prev = words[words[i].next = wordindex].prev;
    words[wordindex].prev = words[words[wordindex].prev].next = i;
}

/*
 * match and complete a word referring to the word list
 */
void complete_word __P1 (char *,dummy)
{
    /*
     * GH: rewritten to allow circulating through history with repetitive command
     *     code stolen from cancan 2.6.3a
     *        curr_word:   index into words[]
     *        comp_len     length of current completition
     *        root_len     length of the root word (before the completition)
     *        root         start of the root word
     */	
    
    static int curr_word, comp_len = 0, root_len = 0;
    char *root, *p;
    int k, n;
    
    /* find word start */
    if (last_edit_cmd == (function_any)complete_word && comp_len) {
	k = comp_len;
	input_moveto(pos - k);
	n = pos - root_len;
    } else {
	for (n = pos; n > 0 && !IS_DELIM(edbuf[n - 1]); n--)
	    ;
	k = 0;
	curr_word = wordindex;
	root_len = pos - n;
    }
    root = edbuf + n; comp_len = 0;
    
    /* k = chars to delete,  n = position of starting word */
    
    /* scan word list for next match */
    while ((p = words[curr_word = words[curr_word].next].word)) {
	if (!strncasecmp(p, root, root_len) &&
	    *(p += root_len) &&
	    (n = strlen(p)) + edlen < BUFSIZE) {
	    comp_len = n;
	    for (; k && n; k--, n--)
		input_overtype_follow(*p++);
	    if (n > 0)
		input_insert_follow_chars(p, n);
	    break;
	}
    }
    if (k > 0)
	input_delete_nofollow_chars(k);
    
    /* delete duplicate instances of the word */
    if (p && !(words[k = curr_word].flags & WORD_UNIQUE)) {
	words[k].flags |= WORD_UNIQUE;
	p = words[k].word;  
	n = words[k].next;
	while (words[k = n].word) {
	    n = words[k].next;
	    if (!strcmp(p, words[k].word)) {
		demote_word(k);
		free(words[k].word);
		words[k].word = 0;
		words[curr_word].flags |= words[k].flags;	/* move retain flag */
		if ((words[k].flags &= WORD_UNIQUE))
		    break;
	    }
	}
    }
}

/*
 * match and complete entire lines backwards in history
 * GH: made repeated complete_line cycle through history
 */
void complete_line __P1 (char *,dummy)
{
    static int curr_line = MAX_HIST-1, root_len = 0, first_line = 0;
    int i;
    
    if (last_edit_cmd != (function_any)complete_line) {
	root_len = edlen;
	first_line = curr_line = curline;
    }
    
    for (i = curr_line - 1; i != curr_line; i--) {
	if (i < 0) i = MAX_HIST - 1;
	if (i == first_line)
	    break;
	if (hist[i] && !strncmp(edbuf, hist[i], root_len))
	    break;
    }
    if (i != curr_line) {
	clear_input_line(0);
	if (i == first_line) {
	    edbuf[root_len] = 0;
	    edlen = root_len;
	} else {
	    strcpy(edbuf, hist[i]);
	    edlen = strlen(edbuf);
	}
	pos = edlen;
	curr_line = i;
    }
}

/*
 * GH: word history handling stolen from cancan 2.6.3a
 */

static void default_completions __P0 (void)
{
    char buf[BUFSIZE];
    cmdstruct *p;
    int i;
    for (i = 0, buf[0] = '#', p = commands; p->name; p++)
	if (p->funct /*&& strlen(p->name) >= 3*/ ) {
	    if (++i >= MAX_WORDS) break;
	    strcpy(buf + 1, p->name);
	    if (!(words[i].word = my_strdup(buf)))
		syserr("malloc");
	    words[i].flags = WORD_UNIQUE | WORD_RETAIN;
	}
    for (i = MAX_WORDS; i--; words[i].prev = i - 1, words[i].next = i + 1)
	;
    words[0].prev = MAX_WORDS - 1;
    words[MAX_WORDS - 1].next = 0;
}

/*
 * put word in word completion ring
 */
void put_word __P1 (char *,s)
{
    int r = wordindex;
    if (!(words[r].word = my_strdup(s))) {
	errmsg("malloc");
	return;
    }
    words[r].flags = 0;
    while (words[r = words[r].prev].flags & WORD_RETAIN)
	;
    demote_word(r);
    wordindex = r;
    if (words[r].word) { 
	free(words[r].word); 
	words[r].word = 0; 
    }
}

/*
 * GH: set delimeters[DELIM_CUSTOM]
 */
void set_custom_delimeters __P1 (char *,s)
{
    char *old = delim_list[DELIM_CUSTOM];
    if (!(delim_list[DELIM_CUSTOM] = my_strdup(s)))
	errmsg("malloc");
    else {
	if (old)
	    free(old);
	delim_len[DELIM_CUSTOM] = strlen(s);
	delim_mode = DELIM_CUSTOM;
    }
}

/*
 * enter a line
 */
void enter_line __P1 (char *,dummy)
{
    char *p;
    
    if (line_status == 0)
	input_moveto(edlen);
    else {
	if (prompt_status != 0)
	    col0 = 0;
	draw_input_line();
    }
    PRINTF("%s\n", edattrend);
    
    line0 = CURLINE(edlen);
    if (line0 < lines - 1) line0++;
    
    if (recordfile)
	fprintf(recordfile, "%s\n", edbuf);
    
    col0 = error = pos = line_status = 0;
    
    if (!*edbuf || (verbatim && *edbuf != '#'))
	tcp_write(tcp_fd, edbuf);
    else
	parse_user_input(edbuf, 1);    
    history_done = 0;
    
    /* don't put identical lines in history, nor empty ones */
    p = hist[curline ? curline - 1 : MAX_HIST - 1];
    if (!p || (edlen > 0 && strcmp(edbuf, p)))
	put_history(edbuf);
    pickline = curline;
    if (*inserted_next) {
	strcpy(edbuf, inserted_next);
	inserted_next[0] = '\0';
	line_status = 1;
    } else if (*prefixstr) {
	strcpy(edbuf, prefixstr);
	line_status = 1;
    } else
	edbuf[0] = '\0';
    pos = edlen = strlen(edbuf);
}

/*
 * move one word forward
 */
void next_word __P1 (char *,dummy)
{
    int i;
    for (i = pos; edbuf[i] && !isalnum(edbuf[i]); i++)
	;
    while (isalnum(edbuf[i]))
	i++;
    input_moveto(i);
}

/*
 * move one word backward
 */
void prev_word __P1 (char *,dummy)
{
    int i;
    for (i = pos; i && !isalnum(edbuf[i - 1]); i--)
	;
    while (i && isalnum(edbuf[i - 1]))
	i--;
    input_moveto(i);
}

/*
 * delete word to the right
 */
void del_word_right __P1 (char *,dummy)
{
    int i;
    for (i = pos; edbuf[i] && !isalnum(edbuf[i]); i++)
	;
    while (isalnum(edbuf[i]))
	i++;
    input_delete_nofollow_chars(i - pos);
}

/*
 * delete word to the left
 */
void del_word_left __P1 (char *,dummy)
{
    int i;
    for (i = pos; i && !isalnum(edbuf[i - 1]); i--)
	;
    while (i && isalnum(edbuf[i - 1]))
	i--;
    i = pos - i;
    input_moveto(pos - i);
    input_delete_nofollow_chars(i);
}

/*
 * GH: make word upcase
 */
void upcase_word __P1 (char *,dummy)
{
    int opos = pos;
    int npos = pos;
    
    if (last_edit_cmd == (function_any)upcase_word)
	npos = 0;
    else {
	while (npos > 0 && IS_DELIM(edbuf[npos])) npos--;
	while (npos > 0 && !IS_DELIM(edbuf[npos - 1])) npos--;
    }
    input_moveto(npos);
    while (!IS_DELIM(edbuf[npos]) ||
	   (last_edit_cmd == (function_any)upcase_word && edbuf[npos]))
	input_overtype_follow(toupper(edbuf[npos++]));
    input_moveto(opos);
}

/*
 * GH: make word downcase
 */
void downcase_word __P1 (char *,dummy)
{
    int opos = pos;
    int npos = pos;

    if (last_edit_cmd == (function_any)downcase_word)
	npos = 0;
    else {
	while (npos > 0 && IS_DELIM(edbuf[npos])) npos--;
	while (npos > 0 && !IS_DELIM(edbuf[npos - 1])) npos--;
    }
    input_moveto(npos);
    while (!IS_DELIM(edbuf[npos]) ||
	   (last_edit_cmd == (function_any)downcase_word && edbuf[npos])) {
	input_overtype_follow(tolower(edbuf[npos++]));
    }
    input_moveto(opos);
}

/*
 * get previous line from history list
 */
void prev_line __P1 (char *,dummy)
{
    int i = pickline - 1;
    if (i < 0) i = MAX_HIST - 1;
    if (hist[i]) {
	if (hist[pickline] && strcmp(hist[pickline], edbuf)) {
	    free(hist[pickline]);
	    hist[pickline] = NULL;
	}
	if (!hist[pickline]) {
	    if (!(hist[pickline] = my_strdup(edbuf))) {
		errmsg("malloc");
		return;
	    }
	}
	pickline = i;
	clear_input_line(0);
	strcpy(edbuf, hist[pickline]);
	pos = edlen = strlen(edbuf);
    }
}

/*
 * get next line from history list
 */
void next_line __P1 (char *,dummy)
{
    int i = pickline + 1;
    if (i == MAX_HIST) i = 0;
    if (hist[i]) {
	if (hist[pickline] && strcmp(hist[pickline], edbuf)) {
	    free(hist[pickline]);
	    hist[pickline] = NULL;
	}
	if (!hist[pickline]) {
	    if (!(hist[pickline] = my_strdup(edbuf))) {
		errmsg("malloc");
		return;
	    }
	}
	pickline = i;
	clear_input_line(0);
	strcpy(edbuf, hist[pickline]);
	edlen = pos = strlen(edbuf);
    }
}

/*
 * move one char backward
 */
void prev_char __P1 (char *,dummy)
{
    input_moveto(pos-1);
}

/*
 * move one char forward
 */
void next_char __P1 (char *,dummy)
{
    input_moveto(pos+1);
}

/*
 * Flash cursor at parentheses that matches c inserted before current pos
 */
static void flashparen __P1 (char,c)
{
    int lev, i;
    if (line_status != 0)
	return;
    for (i = pos - 1, lev = 0; i >= 0; i--) {
	if (ISRPAREN(edbuf[i])) {
	    lev++;
	} else if (ISLPAREN(edbuf[i])) {
	    lev--;
	    if (!lev) {
		if (LPAREN(c) == edbuf[i])
		    break;
		else
		    i = -1;
	    }
	}
    }
    if (i >= 0) {
	tty_gotoxy_opt(CURCOL(pos), CURLINE(pos), CURCOL(i), CURLINE(i));
	flashback = 1;
	excursion = i;
    }
}

/*
 * put cursor back where it belongs
 */
void putbackcursor __P0 (void)
{
    if (line_status == 0)
	tty_gotoxy_opt(CURCOL(excursion), CURLINE(excursion), CURCOL(pos), CURLINE(pos));
    flashback = 0;
}

/*
 * insert a typed character on screen (if it is printable)
 */
void insert_char __P1 (char,c)
{
    if (((c & 0x80) || (c >= ' ' && c <= '~')) && edlen < BUFSIZE - 2) {
	if (flashback) putbackcursor();
	input_insert_follow_chars(&c, 1);
	if (ISRPAREN(c))
	    flashparen(c);
    }
}

static void insert_string __P1 (char *,arg)
{
    char buf[BUFSIZE];
    int len;
    
    if (!arg || !*arg)
	return;
    
    my_strncpy(buf, arg, BUFSIZE-1);
    unescape(buf);
    len = strlen(buf);
    
    if (len > 1) {
	if (flashback) putbackcursor();
	input_insert_follow_chars(buf, len);
    } else if (len == 1)
	insert_char(buf[0]); /* also flash matching parentheses */
}

/*
 * execute string as if typed
 */
void key_run_command __P1 (char *,cmd)
{
    clear_input_line(opt_compact && !opt_keyecho);
    if (opt_keyecho) {
	tty_printf("%s%s%s\n", edattrbeg, cmd, edattrend);
    } else if (!opt_compact)
        tty_putc('\n');
    
    status(1);
    error = 0;
    
    if (recordfile)
	fprintf(recordfile, "%s\n", edbuf);
    
    parse_instruction(cmd, 1, 0, 1);
    history_done = 0;
}

void edit_bootstrap __P0 (void)
{
    default_completions();
}

