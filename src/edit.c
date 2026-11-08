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

#include <assert.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>

#include <sys/time.h>
#include <sys/types.h>

#include "defines.h"
#include "main.h"
#include "utils.h"
#include "cmd.h"
#include "edit.h"
#include "tcp.h"
#include "tty.h"
#include "eval.h"
#include "log.h"
#include "charset.h"
#include "width.h"

#ifdef USE_LOCALE
#  include <wctype.h>
#endif
#ifdef HAVE_LIBGRAPHEME
#  include <grapheme.h>
#endif

static void insert_string(char *arg);

/* history buffer */
char *hist[MAX_HIST];	/* saved history lines */
int curline = 0;	/* current history line */
int pickline = 0;	/* line to pick history from */

/* word completion list */
wordnode words[MAX_WORDS];
int wordindex = 0;

const edit_function internal_functions[] = {
    { 0 },
    { .name = "&enter-line", .funct = enter_line,
      .help = "execute the input line" },
    { .name = "&complete-word", .funct = complete_word,
      .help = "complete the word from the history" },
    { .name = "&complete-line", .funct = complete_line,
      .help = "complete the line from the history" },
    { .name = "&del-char-left", .funct = del_char_left,
      .help = "delete the character left of the cursor" },
    { .name = "&del-char-right", .funct = del_char_right,
      .help = "delete the character under the cursor" },
    { .name = "&prev-char", .funct = prev_char,
      .help = "move back one character" },
    { .name = "&prev-line", .funct = prev_line,
      .help = "show the previous history line" },
    { .name = "&next-char", .funct = next_char,
      .help = "move forward one character" },
    { .name = "&next-line", .funct = next_line,
      .help = "show the next history line" },
    { .name = "&to-history", .funct = to_history,
      .help = "put the line in the history, and clear it" },
    { .name = "&clear-line", .funct = clear_line,
      .help = "clear the input line" },
    { .name = "&redraw-line", .funct = redraw_line,
      .help = "redraw the prompt and the input line" },
    { .name = "&redraw-line-noprompt", .funct = redraw_line_noprompt,
      .help = "redraw the input line without the prompt" },
    { .name = "&begin-of-line", .funct = begin_of_line,
      .help = "move to the start of the line" },
    { .name = "&end-of-line", .funct = end_of_line,
      .help = "move to the end of the line" },
    { .name = "&kill-to-eol", .funct = kill_to_eol,
      .help = "kill to the end of the line" },
    { .name = "&kill-to-bol", .funct = kill_to_bol,
      .help = "kill to the start of the line" },
    { .name = "&yank", .funct = yank,
      .help = "insert the last killed text" },
    { .name = "&yank-pop", .funct = yank_pop,
      .help = "replace the text yanked with an older kill" },
    { .name = "&reverse-search", .funct = reverse_search,
      .help = "search the history backward" },
    { .name = "&forward-search", .funct = forward_search,
      .help = "search the history forward" },
    { .name = "&abort", .funct = abort_edit,
      .help = "stop searching" },
    { .name = "&transpose", .funct = transpose_chars,
      .help = "swap the two characters at the cursor" },
    { .name = "&transpose-words", .funct = transpose_words,
      .help = "swap the two words at the cursor" },
    { .name = "&suspend", .funct = suspend_key,
      .help = "suspend powwow" },
    { .name = "&del-word-left", .funct = del_word_left,
      .help = "kill the word left of the cursor" },
    { .name = "&del-unix-word-left", .funct = del_unix_word_left,
      .help = "kill back to the previous space" },
    { .name = "&del-word-right", .funct = del_word_right,
      .help = "kill the word right of the cursor" },
    { .name = "&prev-word", .funct = prev_word,
      .help = "move back one word" },
    { .name = "&upcase-word", .funct = upcase_word,
      .help = "uppercase to the end of the word" },
    { .name = "&downcase-word", .funct = downcase_word,
      .help = "lowercase to the end of the word" },
    { .name = "&capitalize-word", .funct = capitalize_word,
      .help = "capitalize to the end of the word" },
    { .name = "&next-word", .funct = next_word,
      .help = "move forward one word" },
    { .name = "&insert-string", .funct = insert_string,
      .help = "insert the text after the function name" },
    { 0 }
};

int lookup_edit_name(char *name, char **arg)
{
    int i, len, flen;
    const char *fname;
    char *extra = NULL;

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

int lookup_edit_function(function_str funct)
{
    int i;
    function_str ffunct;

    for (i = 1; (ffunct = internal_functions[i].funct); i++)
	if (funct == ffunct)
	    return i;

    return 0;
}

/* return pointer to any unterminated escape code at the end of s */
static char *find_partial_esc(char *s)
{
    char *esc = strrchr(s, '\033');
    if (esc == NULL)
        return NULL;

    unsigned char *p = (unsigned char *)esc + 1;
    if (*p == '\0')
        return esc;             /* lone ESC */
    if (*p != '[')
        return NULL;            /* two-character escape */
    /* control sequence: parameter and intermediate bytes, then a
     * final byte */
    for (++p; *p >= 0x20 && *p <= 0x3f; ++p)
        ;
    return *p == '\0' ? esc : NULL;
}

/* byte offset of the cell after/before byte offset 'p' of edbuf */
static int next_cell(int p)
{
    return utf8_next_cell(edbuf + p, edbuf + edlen) - edbuf;
}

static int prev_cell(int p)
{
    return utf8_prev_cell(edbuf, edbuf + p) - edbuf;
}

/* is the character at byte offset 'p' of edbuf part of a word?
 * all non-ASCII characters are, to behave the same in any locale */
static int word_at(int p)
{
    uint32_t cp;
    utf8_get(edbuf + p, edlen - p, &cp);
    if (cp >= 0x80)
        return 1;
    return isalnum(cp);
}

/*
 * redisplay the prompt
 * assume cursor is at beginning of line
 */
void draw_prompt(void)
{
    if (promptlen && prompt_status == 1) {
        char *esc, *pstr;
	int e = error;
	error = 0;
	marked_prompt = ptraddsubst_and_marks(marked_prompt, prompt->str);
	if (MEM_ERROR) { promptzero(); out_of_memory(); return; }

        /* if prompt ends in unterminated escape code, do not print
         * that part */
        pstr = ptrdata(marked_prompt);
        esc = find_partial_esc(pstr);
        if (esc)
            *esc = 0;
        sgr_prompt_draw();
        tty_puts(pstr);
	col0 = printstrlen(pstr);
        if (esc)
            *esc = '\033';

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
void clear_input_line(int deleteprompt)
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
void clear_line(char *dummy)
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
void draw_input_line(void)
{
    if (line_status == 0 || linemode & LM_NOECHO)
	return;

    tty_puts(edattrbeg);

    if (edlen)
        input_draw();
    line_status = 0;
}

/*
 * redraw the input line
 */
void redraw_line(char *dummy)
{
    clear_input_line(1);
}

/*
 * redraw the input line, clearing the prompt
 */
void redraw_line_noprompt(char *dummy)
{
    clear_input_line(0);
    tty_putc('\n');
    if (line0 < lines - 1)
	line0++;
    status(-1);
}

/*
 * transpose two words to the left
 */
void transpose_words(char *dummy)
{
    /* other refers to the word to the left, this is the one we are at */

    int this_so, other_so, this_eo, other_eo;
    char buf[BUFSIZE];
    int n;

    if (pos > 2) {

	this_eo = this_so = pos;
	/* optionally traceback to find a word */
	while (this_so != 0 && (this_so == edlen || IS_DELIM(edbuf[this_so])))
	    this_so--;

	/* now find where the current word ends */
	while (this_eo < edlen && !IS_DELIM(edbuf[this_eo]))
	    this_eo++;

	/* found a word; now find its start */
	while (this_so > 0 && !IS_DELIM(edbuf[this_so - 1]))
	    this_so--;

	if (this_so < 2)
	    return;		/* impossible that there's another word */

	other_so = this_so - 1;
	while (other_so >= 0 && IS_DELIM(edbuf[other_so]))
	    other_so--;
	if (other_so < 0)
	    return;
	other_eo = other_so + 1;
	while (other_so > 0 && !IS_DELIM(edbuf[other_so - 1]))
	    other_so--;

	n = sprintf(buf, "%.*s%.*s%.*s",
		    this_eo - this_so, edbuf + this_so,
		    this_so - other_eo, edbuf + other_eo,
		    other_eo - other_so, edbuf + other_so);

	input_replace(other_so, this_eo - other_so, buf, n);
    }
}

/*
 * suspend powwow (key binding)
 */
void suspend_key(char *dummy)
{
    suspend_powwow(0);
}

/*
 * transpose two characters to the left
 */
void transpose_chars(char *dummy)
{
    /* swap the cells [i, j) and [j, k) */
    int i, j, k;
    char buf[2 * BUFSIZE];

    if (pos == 0 || (pos == edlen && prev_cell(pos) == 0))
        return;
    if (pos < edlen) {
        j = pos;
        k = next_cell(pos);
    } else {
        k = pos;
        j = prev_cell(pos);
    }
    i = prev_cell(j);

    memcpy(buf, edbuf + j, k - j);
    memcpy(buf + k - j, edbuf + i, j - i);
    input_replace(i, k - i, buf, k - i);
}

/*
 * the kill ring: the text deleted by the kill commands, to yank it back
 */
#define KILL_RING_SIZE 10
static char *kill_ring[KILL_RING_SIZE];
static int kill_last;                   /* index of the last kill */
static int yank_index;                  /* index of the text yanked last */
static int yank_start;                  /* where that text starts */

static bool is_kill_cmd(function_str funct)
{
    return (funct == kill_to_eol || funct == kill_to_bol
            || funct == del_word_left || funct == del_word_right
            || funct == del_unix_word_left);
}

/*
 * save the 'len' bytes at 'p' in the kill ring; if the previous command
 * also killed, add them to its kill, before it if 'before'; nothing is
 * saved in no-echo mode, so that passwords are not kept
 */
static void kill_text(const char *p, int len, bool before)
{
    if (len <= 0 || (linemode & LM_NOECHO))
        return;
    if (!is_kill_cmd(last_edit_cmd) || kill_ring[kill_last] == NULL) {
        kill_last = (kill_last + 1) % KILL_RING_SIZE;
        free(kill_ring[kill_last]);
        kill_ring[kill_last] = NULL;
    }
    char *old = kill_ring[kill_last];
    const char *old_text = old != NULL ? old : "";  /* not NULL for memcpy */
    size_t old_len = strlen(old_text);
    char *s = malloc(old_len + len + 1);
    if (s == NULL) {
        out_of_memory();
        return;
    }
    if (before) {
        memcpy(s, p, len);
        memcpy(s + len, old_text, old_len);
    } else {
        memcpy(s, old_text, old_len);
        memcpy(s + old_len, p, len);
    }
    s[old_len + len] = '\0';
    free(old);
    kill_ring[kill_last] = s;
}

/* kill the 'n' bytes to the left or right of the cursor */
static void kill_left(int n)
{
    kill_text(edbuf + pos - n, n, true);
    input_moveto(pos - n);
    input_delete_nofollow_chars(n);
}

static void kill_right(int n)
{
    kill_text(edbuf + pos, n, false);
    input_delete_nofollow_chars(n);
}

/*
 * insert the last killed text
 */
void yank(char *dummy)
{
    if ((linemode & LM_NOECHO) || kill_ring[kill_last] == NULL)
        return;
    yank_index = kill_last;
    yank_start = pos;
    input_insert_follow_chars(kill_ring[kill_last],
                              strlen(kill_ring[kill_last]));
}

/*
 * replace the text just yanked with the kill before it
 */
void yank_pop(char *dummy)
{
    if ((linemode & LM_NOECHO)
        || (last_edit_cmd != yank && last_edit_cmd != yank_pop)
        || yank_start > pos)
        return;
    int i = yank_index;
    do {
        i = (i + KILL_RING_SIZE - 1) % KILL_RING_SIZE;
    } while (kill_ring[i] == NULL);
    yank_index = i;
    input_replace(yank_start, pos - yank_start,
                  kill_ring[i], strlen(kill_ring[i]));
}

/*
 * kill everything before the cursor
 */
void kill_to_bol(char *dummy)
{
    kill_left(pos);
}

/*
 * erase everything to the end of line
 */
void kill_to_eol(char *dummy)
{
    kill_text(edbuf + pos, edlen - pos, false);
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
void end_of_line(char *dummy)
{
    input_moveto(edlen);
}

/*
 * move cursor to beginning of line
 */
void begin_of_line(char *dummy)
{
    input_moveto(0);
}

/*
 * delete a character to the right
 */
void del_char_right(char *dummy)
{
    input_delete_nofollow_chars(next_cell(pos) - pos);
}

/*
 * delete a character to the left
 */
void del_char_left(char *dummy)
{
    if (pos) {
        int p = pos;
	input_moveto(prev_cell(pos));
	input_delete_nofollow_chars(p - pos);
    }
}

/*
 * move a line into history, but don't do anything else
 */
void to_history(char *dummy)
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
void put_history(char *str)
{
    char *p;
    if (hist[curline]) free(hist[curline]);
    if (!(hist[curline] = my_strdup(str))) {
	out_of_memory();
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
static void demote_word(int i)
{
    words[words[i].prev].next = words[i].next;
    words[words[i].next].prev = words[i].prev;
    words[i].prev = words[words[i].next = wordindex].prev;
    words[wordindex].prev = words[words[wordindex].prev].next = i;
}

static struct {
    int size, used;
    char **words;
} static_words;

static int compl_next_word(int i)
{
    if (i < 0) {
    go_static:
        --i;
        if (-i - 1 >= static_words.used)
            i = wordindex;
    } else {
        i = words[i].next;
        if (i == wordindex || words[i].word == NULL) {
            i = 0;
            goto go_static;
        }
    }
    return i;
}

static char *compl_get_word(int i)
{
    assert(i < MAX_WORDS);
    return i < 0 ? static_words.words[-i - 1] : words[i].word;
}

/*
 * match and complete a word referring to the word list
 */
void complete_word(char *dummy)
{
    /*
     * repeating the command cycles through the words
     *     curr_word    index into words[]
     *     comp_len     length of current completion
     *     root_len     length of the root word (before the completion)
     *     root         start of the root word
     */

    static int curr_word, comp_len = 0, root_len = 0;
    char *root, *p;
    int k, n;

    /* find word start */
    if (last_edit_cmd == complete_word && comp_len) {
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
    while ((p = compl_get_word(curr_word = compl_next_word(curr_word)))) {
	if (!strncasecmp(p, root, root_len) &&
	    *(p += root_len) &&
	    (n = strlen(p)) + edlen < BUFSIZE) {
	    comp_len = n;
	    input_replace(pos, k, p, n);
	    k = 0;
	    break;
	}
    }
    if (k > 0)
	input_delete_nofollow_chars(k);

    /* delete duplicate instances of the word */
    if (p && curr_word >= 0
        && !(words[k = curr_word].flags & WORD_UNIQUE)) {
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
 * repeating the command cycles through the history
 */
void complete_line(char *dummy)
{
    static int curr_line = MAX_HIST-1, root_len = 0, first_line = 0;
    int i;

    if (last_edit_cmd != complete_line) {
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

static void default_completions(void)
{
    char buf[BUFSIZE];
    cmdstruct *p;
    int i;
    /* TODO: add some way to handle new commands going in the default
     * completions list */
    for (i = 0, buf[0] = '#', p = commands; p != NULL; p = p -> next)
	if (p->funct) {
	    strcpy(buf + 1, p->name);
            put_static_word(buf);
	}
    /* init 'words' double-linked list */
    for (i = MAX_WORDS; i--; words[i].prev = i - 1, words[i].next = i + 1)
	;
    words[0].prev = MAX_WORDS - 1;
    words[MAX_WORDS - 1].next = 0;
}

void put_static_word(char *s)
{
    if (static_words.used >= static_words.size) {
        int size = static_words.size;
        do {
            size = size != 0 ? size * 2 : 16;
        } while (static_words.used >= size);
        char **new_words = realloc(static_words.words,
                                   sizeof(new_words[0]) * size);
        if (new_words == NULL) {
            out_of_memory();
            return;
        }
        static_words.words = new_words;
        static_words.size = size;
    }

    if ((s = my_strdup(s)) == NULL) {
        out_of_memory();
        return;
    }
    static_words.words[static_words.used++] = s;
}

/*
 * put word in word completion ring
 */
void put_word(char *s)
{
    int r = wordindex;
    if (!(words[r].word = my_strdup(s))) {
	out_of_memory();
	return;
    }
    words[r].flags = 0;
    r = words[r].prev;
    demote_word(r);
    wordindex = r;
    if (words[r].word) {
	free(words[r].word);
	words[r].word = 0;
    }
}

/*
 * set delimeters[DELIM_CUSTOM]
 */
void set_custom_delimeters(char *s)
{
    char *old = delim_list[DELIM_CUSTOM];
    if (!(delim_list[DELIM_CUSTOM] = my_strdup(s)))
	out_of_memory();
    else {
	if (old)
	    free(old);
	delim_len[DELIM_CUSTOM] = strlen(s);
	delim_mode = DELIM_CUSTOM;
    }
}

void fmt_next_edit(const char *fmt, ...)
{
    va_list va;
    va_start(va, fmt);
    int n = vsnprintf(inserted_next, sizeof inserted_next, fmt, va);
    if (n >= sizeof inserted_next) {
        static const char msg[] = " [TRUNCATED]";
        memcpy(inserted_next + sizeof inserted_next - sizeof msg,
               msg, sizeof msg);
    }
    va_end(va);
}

/*
 * enter a line
 */
void enter_line(char *dummy)
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

    log_record_line(edbuf);

    col0 = error = pos = line_status = 0;

    if (!*edbuf || (verbatim && *edbuf != '#'))
	tcp_write(current_connection, edbuf);
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
void next_word(char *dummy)
{
    int i;
    for (i = pos; i < edlen && !word_at(i); i = next_cell(i))
	;
    while (i < edlen && word_at(i))
	i = next_cell(i);
    input_moveto(i);
}

/*
 * move one word backward
 */
void prev_word(char *dummy)
{
    int i;
    for (i = pos; i && !word_at(prev_cell(i)); i = prev_cell(i))
	;
    while (i && word_at(prev_cell(i)))
	i = prev_cell(i);
    input_moveto(i);
}

/*
 * delete word to the right
 */
void del_word_right(char *dummy)
{
    int i;
    for (i = pos; i < edlen && !word_at(i); i = next_cell(i))
	;
    while (i < edlen && word_at(i))
	i = next_cell(i);
    kill_right(i - pos);
}

/*
 * delete word to the left
 */
void del_word_left(char *dummy)
{
    int i;
    for (i = pos; i && !word_at(prev_cell(i)); i = prev_cell(i))
	;
    while (i && word_at(prev_cell(i)))
	i = prev_cell(i);
    kill_left(pos - i);
}

/*
 * delete to the start of the space-separated word to the left, like
 * unix-word-rubout in readline
 */
void del_unix_word_left(char *dummy)
{
    int i = pos;
    while (i > 0 && (edbuf[i - 1] == ' ' || edbuf[i - 1] == '\t'))
        --i;
    while (i > 0 && edbuf[i - 1] != ' ' && edbuf[i - 1] != '\t')
        --i;
    kill_left(pos - i);
}

enum case_change { CASE_UPPER, CASE_LOWER, CASE_CAPITALIZE };

#ifndef HAVE_LIBGRAPHEME
/* the case mappings of one character */
static uint32_t to_upper(uint32_t cp)
{
#ifdef USE_LOCALE
    return towupper(cp);
#else
    return cp < 0x80 ? (uint32_t)toupper(cp) : cp;
#endif
}

static uint32_t to_lower(uint32_t cp)
{
#ifdef USE_LOCALE
    return towlower(cp);
#else
    return cp < 0x80 ? (uint32_t)tolower(cp) : cp;
#endif
}

/* the titlecase differs from the uppercase only for these letters */
static uint32_t to_title(uint32_t cp)
{
    switch (cp) {
    case U'Ǆ': case U'ǅ': case U'ǆ':
        return U'ǅ';
    case U'Ǉ': case U'ǈ': case U'ǉ':
        return U'ǈ';
    case U'Ǌ': case U'ǋ': case U'ǌ':
        return U'ǋ';
    case U'Ǳ': case U'ǲ': case U'ǳ':
        return U'ǲ';
    }
    /* Georgian Mkhedruli letters are their own titlecase */
    if ((cp >= U'ა' && cp <= U'ჺ') || (cp >= U'ჽ' && cp <= U'ჿ'))
        return cp;
    return to_upper(cp);
}

static bool is_letter(uint32_t cp)
{
#ifdef USE_LOCALE
    return iswalpha(cp);
#else
    return cp < 0x80 && isalpha(cp);
#endif
}

/*
 * write the UTF-8 text from 'src' to 'dst' with its case changed, one
 * character at a time; return the length of the result
 */
static int change_case(enum case_change change, const char *src, int len,
                       char *dst)
{
    bool title = change == CASE_CAPITALIZE;
    bool after_letter = false;
    int n = 0;
    for (int i = 0; i < len; ) {
        uint32_t cp;
        i += utf8_get(src + i, len - i, &cp);
        bool letter = is_letter(cp);
        if (change == CASE_UPPER) {
            cp = to_upper(cp);
        } else if (title && letter) {
            cp = to_title(cp);
            title = false;
        } else if (cp == U'Σ' && after_letter) {
            /* final sigma at the end of a word */
            uint32_t next = U' ';
            if (i < len)
                utf8_get(src + i, len - i, &next);
            cp = is_letter(next) ? U'σ' : U'ς';
        } else {
            cp = to_lower(cp);
        }
        after_letter = letter;
        n += utf8_put(cp, dst + n);
    }
    return n;
}
#endif

/*
 * change the case of the word at or after the cursor, from the cursor
 * to the end of the word, and move the cursor there, like readline; the
 * UTF-8 length of the text may change, such as from ß to SS
 */
static void change_case_word(enum case_change change)
{
    int start = pos;
    while (start < edlen && !word_at(start))
        start = next_cell(start);
    int end = start;
    while (end < edlen && word_at(end))
        end = next_cell(end);

    /* a character becomes at most three, each no longer in UTF-8 */
    char buf[3 * BUFSIZE];
#ifdef HAVE_LIBGRAPHEME
    size_t n = 0;
    switch (change) {
    case CASE_UPPER:
        n = grapheme_to_uppercase_utf8(edbuf + start, end - start,
                                       buf, sizeof(buf));
        break;
    case CASE_LOWER:
        n = grapheme_to_lowercase_utf8(edbuf + start, end - start,
                                       buf, sizeof(buf));
        break;
    case CASE_CAPITALIZE:
        n = grapheme_to_titlecase_utf8(edbuf + start, end - start,
                                       buf, sizeof(buf));
        break;
    }
    if (n >= sizeof(buf))
        n = sizeof(buf) - 1;
#else
    int n = change_case(change, edbuf + start, end - start, buf);
#endif
    input_replace(start, end - start, buf, n);
}

/*
 * make word upcase
 */
void upcase_word(char *dummy)
{
    change_case_word(CASE_UPPER);
}

/*
 * make word downcase
 */
void downcase_word(char *dummy)
{
    change_case_word(CASE_LOWER);
}

/*
 * capitalize word
 */
void capitalize_word(char *dummy)
{
    change_case_word(CASE_CAPITALIZE);
}

/*
 * get previous line from history list
 */
void prev_line(char *dummy)
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
		out_of_memory();
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
void next_line(char *dummy)
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
		out_of_memory();
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
 * incremental history search, like C-r and C-s in readline: while it is
 * active, the input line shows the search and the line found, and the
 * line being edited is kept in 'saved'
 */
static struct {
    bool active, reverse, failed;
    char query[BUFSIZE];
    int query_len;
    int origin;                 /* history index of the line being edited */
    char saved[BUFSIZE];
    int saved_pos;
    int index, offset;          /* where the line found matches */
} isearch;
static char last_query[BUFSIZE];  /* repeated by C-r or C-s with no query */

static const char *isearch_line(int index)
{
    return index == isearch.origin ? isearch.saved : hist[index];
}

/*
 * find the query from 'offset' in history line 'index', backward or
 * forward, and then in the lines beyond it
 */
static void isearch_find(int index, int offset)
{
    for (;;) {
        const char *line = isearch_line(index);
        if (line == NULL)
            break;
        int found = -1;
        int len = strlen(line);
        if (isearch.query_len == 0) {
            if (offset >= 0)
                found = offset < len ? offset : len;
        } else if (isearch.reverse) {
            for (const char *p = line;
                 (p = strstr(p, isearch.query)) != NULL && p - line <= offset;
                 ++p)
                found = p - line;
        } else if (offset <= len) {
            const char *p = strstr(line + offset, isearch.query);
            if (p != NULL)
                found = p - line;
        }
        if (found >= 0) {
            isearch.index = index;
            isearch.offset = found;
            isearch.failed = false;
            return;
        }
        if (isearch.reverse) {
            index = (index + MAX_HIST - 1) % MAX_HIST;
            if (index == curline)
                break;
            offset = INT_MAX;
        } else {
            if (index == curline)
                break;
            index = (index + 1) % MAX_HIST;
            offset = 0;
        }
    }
    isearch.failed = true;
}

/* show the search and the line found in the input line */
static void isearch_show(void)
{
    char buf[2 * BUFSIZE];
    int n = snprintf(buf, sizeof(buf), "(%s%s) '%s': ",
                     isearch.failed ? "failed " : "",
                     isearch.reverse ? "reverse-i-search" : "i-search",
                     isearch.query);
    int cursor = n + isearch.offset;
    n += snprintf(buf + n, sizeof(buf) - n, "%s",
                  isearch_line(isearch.index));
    if (n >= (int)sizeof(buf))
        n = sizeof(buf) - 1;
    input_replace(0, edlen, buf, n);
    input_moveto(cursor < edlen ? cursor : edlen);
}

/*
 * end the search, and edit the line found if 'accept', or else the line
 * that was edited before
 */
static void isearch_end(bool accept)
{
    isearch.active = false;
    if (isearch.query_len > 0)
        strcpy(last_query, isearch.query);

    const char *line = isearch.saved;
    int new_pos = isearch.saved_pos;
    if (accept) {
        new_pos = isearch.offset;
        if (isearch.index != isearch.origin) {
            /* keep the line edited before in the history, like C-p */
            int o = isearch.origin;
            if (hist[o] == NULL || strcmp(hist[o], isearch.saved) != 0) {
                char *s = my_strdup(isearch.saved);
                if (s == NULL) {
                    out_of_memory();
                    return;
                }
                free(hist[o]);
                hist[o] = s;
            }
            pickline = isearch.index;
            line = hist[pickline];
        }
    }
    input_replace(0, edlen, line, strlen(line));
    input_moveto(new_pos);
}

static void isearch_start_or_repeat(bool reverse)
{
    if (!isearch.active) {
        isearch.active = true;
        isearch.reverse = reverse;
        isearch.failed = false;
        isearch.query[isearch.query_len = 0] = '\0';
        isearch.origin = isearch.index = pickline;
        strcpy(isearch.saved, edbuf);
        isearch.saved_pos = isearch.offset = pos;
    } else if (isearch.query_len == 0) {
        /* search again for the last query */
        isearch.reverse = reverse;
        strcpy(isearch.query, last_query);
        isearch.query_len = strlen(last_query);
        if (isearch.query_len > 0)
            isearch_find(isearch.index, isearch.offset);
    } else {
        isearch.reverse = reverse;
        isearch_find(isearch.index,
                     isearch.offset + (reverse ? -1 : 1));
    }
    isearch_show();
}

/*
 * search the history backward or forward for the text typed next
 */
void reverse_search(char *dummy)
{
    isearch_start_or_repeat(true);
}

void forward_search(char *dummy)
{
    isearch_start_or_repeat(false);
}

/*
 * stop searching and edit the line as it was before
 */
void abort_edit(char *dummy)
{
    if (isearch.active)
        isearch_end(false);
}

/* add the UTF-8 character 's' of 'n' bytes to the query, and search again */
static void isearch_add(const char *s, int n)
{
    if (isearch.query_len + n >= BUFSIZE / 2)
        return;
    memcpy(isearch.query + isearch.query_len, s, n);
    isearch.query[isearch.query_len += n] = '\0';
    isearch_find(isearch.index, isearch.offset);
    isearch_show();
}

/* delete the last character of the query, and search again */
static void isearch_del_char(void)
{
    if (isearch.query_len == 0)
        return;
    int n = utf8_prev_cell(isearch.query, isearch.query + isearch.query_len)
        - isearch.query;
    isearch.query[isearch.query_len = n] = '\0';
    isearch.index = isearch.origin;
    isearch.offset = isearch.saved_pos;
    isearch_find(isearch.index, isearch.offset);
    isearch_show();
}

/*
 * while searching, handle the command 'funct' bound to a key: BS deletes
 * from the query, and other commands except the search commands end the
 * search and then run on the line found; return true if 'funct' was
 * handled here
 */
bool isearch_command(function_str funct)
{
    if (!isearch.active
        || funct == reverse_search || funct == forward_search
        || funct == abort_edit)
        return false;
    if (funct == del_char_left) {
        isearch_del_char();
        return true;
    }
    isearch_end(true);
    return false;
}

/*
 * cancel any search, before the input line is used without editing
 */
void isearch_cancel(void)
{
    if (isearch.active)
        isearch_end(false);
}

/*
 * move one char backward
 */
void prev_char(char *dummy)
{
    input_moveto(prev_cell(pos));
}

/*
 * move one char forward
 */
void next_char(char *dummy)
{
    input_moveto(next_cell(pos));
}

static inline bool is_rparen(char c)
{
    return c == ')' || c == ']' || c == '}';
}

static inline bool is_lparen(char c)
{
    return c == '(' || c == '[' || c == '{';
}

/* the left parenthesis matching the right parenthesis c */
static inline char matching_lparen(char c)
{
    return c == ')' ? '(' : (c == ']' ? '[' : '{');
}

/*
 * Flash cursor at parentheses that matches c inserted before current pos
 */
static void flashparen(char c)
{
    int lev, i;
    if (line_status != 0)
	return;
    for (i = pos - 1, lev = 0; i >= 0; i--) {
	if (is_rparen(edbuf[i])) {
	    lev++;
	} else if (is_lparen(edbuf[i])) {
	    lev--;
	    if (!lev) {
		if (matching_lparen(c) == edbuf[i])
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
void putbackcursor(void)
{
    if (line_status == 0)
	tty_gotoxy_opt(CURCOL(excursion), CURLINE(excursion), CURCOL(pos), CURLINE(pos));
    flashback = 0;
}

/*
 * insert a typed byte on screen, once it completes a printable
 * UTF-8 character
 */
/* characters typed, to insert at once (see insert_flush) */
static char insbuf[BUFSIZE];
static int ninsbuf;

/*
 * insert the characters typed so far; inserting all the characters
 * read at once draws a pasted grapheme cluster whole, as some terminals
 * (Terminal.app) draw its beginning with another width
 */
void insert_flush(void)
{
    if (ninsbuf == 0)
        return;
    if (flashback) putbackcursor();
    input_insert_follow_chars(insbuf, ninsbuf);
    ninsbuf = 0;
}

/* insert typed character (byte) 'c' before the next insert_flush() */
void insert_char(char c)
{
    static char pend[UTF8_MAX];
    static int npend, need;
    unsigned char b = c;

    if (npend > 0 && (b & 0xc0) == 0x80) {
        pend[npend++] = b;
        if (npend < need)
            return;
    } else {
        npend = 0;
        if (b >= 0x80) {
            need = utf8_seqlen(b);
            if (need > 1) {
                pend[npend++] = b;
                return;
            }
            return;             /* invalid */
        }
        if (b < ' ' || b > '~')
            return;             /* control character */
        pend[npend++] = b;
    }

    int n = npend;
    npend = 0;
    uint32_t cp;
    if (utf8_get(pend, n, &cp) != n || (cp >= 0x80 && cp < 0xa0))
        return;                 /* invalid or C1 control */

    if (isearch.active) {
        isearch_add(pend, n);
        return;
    }
    if (edlen + ninsbuf < BUFSIZE - n - 1) {
        memcpy(insbuf + ninsbuf, pend, n);
        ninsbuf += n;
	if (is_rparen(c)) {
            insert_flush();
	    flashparen(c);
        }
    }
}

static void insert_string(char *arg)
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
    } else if (len == 1) {
	insert_char(buf[0]); /* also flash matching parentheses */
        insert_flush();
    }
}

/*
 * execute string as if typed
 */
void key_run_command(char *cmd)
{
    clear_input_line(opt_compact && !opt_keyecho);
    if (opt_keyecho) {
	tty_printf("%s%s%s\n", edattrbeg, cmd, edattrend);
    } else if (!opt_compact)
        tty_putc('\n');

    status(1);
    error = 0;

    log_record_line(edbuf);

    parse_instruction(cmd, 1, 0, 1);
    history_done = 0;
}

void edit_bootstrap(void)
{
    default_completions();
}

