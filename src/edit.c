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

#include <dirent.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>

#include "defines.h"
#include "main.h"
#include "attr.h"
#include "utils.h"
#include "cmd.h"
#include "edit.h"
#include "tcp.h"
#include "tty.h"
#include "eval.h"
#include "log.h"
#include "charset.h"
#include "charname.h"
#include "width.h"
#include "tls.h"
#include "secret.h"

#ifdef USE_LOCALE
#  include <wctype.h>
#endif
#ifdef HAVE_LIBGRAPHEME
#  include <grapheme.h>
#endif

static void insert_string(char *arg);

/* history buffer */
char *hist[MAX_HIST];   /* saved history lines */
int curline = 0;        /* current history line */
int pickline = 0;       /* line to pick history from */

/* word completion list */
wordnode words[MAX_WORDS];
int wordindex = 0;

const edit_function internal_functions[] = {
    { 0 },
    { .name = "&begin-of-line", .funct = begin_of_line, .group = "moving",
      .help = "move to the start of the line" },
    { .name = "&end-of-line", .funct = end_of_line, .group = "moving",
      .help = "move to the end of the line" },
    { .name = "&prev-char", .funct = prev_char, .group = "moving",
      .help = "move back one character" },
    { .name = "&next-char", .funct = next_char, .group = "moving",
      .help = "move forward one character" },
    { .name = "&prev-word", .funct = prev_word, .group = "moving",
      .help = "move back one word" },
    { .name = "&next-word", .funct = next_word, .group = "moving",
      .help = "move forward one word" },
    { .name = "&prev-line", .funct = prev_line, .group = "history",
      .help = "show the previous history line" },
    { .name = "&next-line", .funct = next_line, .group = "history",
      .help = "show the next history line" },
    { .name = "&reverse-search", .funct = reverse_search, .group = "history",
      .help = "search the history backward" },
    { .name = "&forward-search", .funct = forward_search, .group = "history",
      .help = "search the history forward" },
    { .name = "&abort", .funct = abort_edit, .group = "history",
      .help = "stop searching" },
    { .name = "&begin-of-history", .funct = begin_of_history,
      .group = "history",
      .help = "show the first history line" },
    { .name = "&end-of-history", .funct = end_of_history,
      .group = "history",
      .help = "show the line being typed, after the history" },
    { .name = "&to-history", .funct = to_history, .group = "history",
      .help = "put the line in the history, and clear it" },
    { .name = "&del-char-left", .funct = del_char_left,
      .group = "changing text",
      .help = "delete the character before the cursor" },
    { .name = "&del-char-right", .funct = del_char_right,
      .group = "changing text",
      .help = "delete the character under the cursor" },
    { .name = "&transpose", .funct = transpose_chars, .group = "changing text",
      .help = "swap the two characters at the cursor" },
    { .name = "&transpose-words", .funct = transpose_words,
      .group = "changing text",
      .help = "swap the two words at the cursor" },
    { .name = "&upcase-word", .funct = upcase_word, .group = "changing text",
      .help = "uppercase to the end of the word" },
    { .name = "&downcase-word", .funct = downcase_word,
      .group = "changing text",
      .help = "lowercase to the end of the word" },
    { .name = "&capitalize-word", .funct = capitalize_word,
      .group = "changing text",
      .help = "capitalize to the end of the word" },
    { .name = "&undo", .funct = undo, .group = "changing text",
      .help = "undo the last change of the line" },
    { .name = "&redo", .funct = redo, .group = "changing text",
      .help = "redo the last change undone" },
    { .name = "&insert-char", .funct = insert_named_char,
      .group = "changing text",
      .help = "insert a character by name or code point" },
    { .name = "&insert-emoji", .funct = insert_emoji,
      .group = "changing text", .help = "insert an emoji by its name" },
    { .name = "&insert-string", .funct = insert_string,
      .group = "changing text",
      .help = "insert the text after the function name" },
    { .name = "&kill-to-eol", .funct = kill_to_eol,
      .group = "killing and yanking",
      .help = "kill to the end of the line" },
    { .name = "&kill-to-bol", .funct = kill_to_bol,
      .group = "killing and yanking",
      .help = "kill to the start of the line" },
    { .name = "&del-word-left", .funct = del_word_left,
      .group = "killing and yanking",
      .help = "kill the word left of the cursor" },
    { .name = "&del-unix-word-left", .funct = del_unix_word_left,
      .group = "killing and yanking",
      .help = "kill back to the previous space" },
    { .name = "&del-word-right", .funct = del_word_right,
      .group = "killing and yanking",
      .help = "kill the word right of the cursor" },
    { .name = "&yank", .funct = yank, .group = "killing and yanking",
      .help = "insert the last killed text" },
    { .name = "&yank-pop", .funct = yank_pop, .group = "killing and yanking",
      .help = "replace the text yanked with an older kill" },
    { .name = "&yank-last-word", .funct = yank_last_word,
      .group = "killing and yanking",
      .help = "insert the last word of the previous line" },
    { .name = "&complete-word", .funct = complete_word, .group = "completion",
      .help = "complete the word from the history" },
    { .name = "&complete-line", .funct = complete_line, .group = "completion",
      .help = "complete the line from the history" },
    { .name = "&enter-line", .funct = enter_line, .group = "other",
      .help = "execute the input line" },
    { .name = "&clear-line", .funct = clear_line, .group = "other",
      .help = "clear the input line" },
    { .name = "&redraw-line", .funct = redraw_line, .group = "other",
      .help = "redraw the prompt and the input line" },
    { .name = "&redraw-line-noprompt", .funct = redraw_line_noprompt,
      .group = "other",
      .help = "redraw the input line without the prompt" },
    { .name = "&suspend", .funct = suspend_key, .group = "other",
      .help = "suspend powwow" },
    { 0 }
};

int lookup_edit_name(char *name, char **arg)
{
    const char *space = strchr(name, ' ');
    const char *end = space != NULL ? space : name + strlen(name);
    int len = end - name;

    for (int i = 1; internal_functions[i].name != NULL; ++i) {
        const char *fname = internal_functions[i].name;
        int flen = strlen(fname);
        if (flen == len && strncmp(name, fname, flen) == 0) {
            char *extra = name + flen;
            if (*extra == ' ')
                ++extra;
            if (*extra == '\0')
                extra = NULL;
            *arg = extra;
            return i;
        }
    }
    *arg = NULL;
    return 0;
}

int lookup_edit_function(function_str funct)
{
    for (int i = 1; internal_functions[i].funct != NULL; ++i) {
        if (funct == internal_functions[i].funct)
            return i;
    }
    return 0;
}

/* return pointer to any unterminated escape code at the end of s */
static char *find_partial_esc(char *s)
{
    char *esc = strrchr(s, ASCII_ESC);
    if (esc == NULL)
        return NULL;

    unsigned char *p = (unsigned char *)esc + 1;
    if (*p == '\0')
        return esc;             /* lone ESC */
    if (*p != '[')
        return NULL;            /* two-character escape */
    /* control sequence: parameter and intermediate bytes, then a
     * final byte */
    for (++p; is_csi_param(*p); ++p)
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
static bool word_at(int p)
{
    uint32_t cp;
    utf8_get(edbuf + p, edlen - p, &cp);
    if (cp > ASCII_MAX)
        return true;
    return isalnum(cp);
}

/*
 * redisplay the prompt
 * assume cursor is at beginning of line
 */
void draw_prompt(void)
{
    if (promptlen != 0 && prompt_status == 1) {
        int e = error;
        error = 0;
        marked_prompt = ptraddsubst_and_marks(marked_prompt, prompt->str);
        if (MEM_ERROR) {
            promptzero();
            print_error(error);
            error = e;
            return;
        }

        /* if prompt ends in unterminated escape code, do not print
         * that part */
        char *pstr = ptrdata(marked_prompt);
        char *esc = find_partial_esc(pstr);
        if (esc != NULL)
            *esc = '\0';
        sgr_prompt_draw();
        tty_puts(pstr);
        col0 = printstrlen(pstr);
        if (esc != NULL)
            *esc = ASCII_ESC;

        error = e;
    }
    prompt_status = 0;
}

/*
 * clear current input line (deleteprompt if to clear also prompt)
 * cursor is left right after the prompt.
 *
 * since we do not expect data from the user at this point,
 * do not print edattrbeg now.
 */
void clear_input_line(bool deleteprompt)
{
    /*
     * be careful: if prompt and/or input line have been erased from screen,
     * pos will be different from the actual cursor position
     */
    if ((edlen != 0 && line_status == 0)
        || (promptlen != 0 && prompt_status == 0 && deleteprompt)) {
        int newcol = deleteprompt ? 0 : col0;
        int realpos = (line_status == 0
                       ? pos
                       : (prompt_status == 0 ? 0 : -col0));

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
    if (edlen == 0)
        return;
    clear_input_line(false);
    pickline = curline;
    *edbuf = '\0';
    edlen = 0;
    pos = 0;
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

    if (edlen != 0)
        input_draw();
    line_status = 0;
}

/*
 * redraw the input line
 */
void redraw_line(char *dummy)
{
    clear_input_line(true);
}

/*
 * redraw the input line, clearing the prompt
 */
void redraw_line_noprompt(char *dummy)
{
    clear_input_line(false);
    tty_putc('\n');
    if (line0 < lines - 1)
        ++line0;
    status(-1);
}

/* the end of the word at or after 'p' */
static int word_end_after(int p)
{
    while (p < edlen && !word_at(p))
        p = next_cell(p);
    while (p < edlen && word_at(p))
        p = next_cell(p);
    return p;
}

/* the start of the word before 'p' */
static int word_start_before(int p)
{
    while (p > edstart && !word_at(prev_cell(p)))
        p = prev_cell(p);
    while (p > edstart && word_at(prev_cell(p)))
        p = prev_cell(p);
    return p;
}

/*
 * swap the word before the cursor with the word at or after it, or the
 * last two words at the end of the line, and move after them, like
 * transpose-words in readline
 */
void transpose_words(char *dummy)
{
    int start2 = word_start_before(word_end_after(pos));
    int end2 = word_end_after(start2);
    int start1 = word_start_before(start2);
    int end1 = word_end_after(start1);
    if (start1 == start2 || start2 < end1) {
        tty_putc(ASCII_BEL);
        return;
    }
    char buf[BUFSIZE];
    int n = snprintf(buf, sizeof(buf), "%.*s%.*s%.*s",
                     end2 - start2, edbuf + start2,
                     start2 - end1, edbuf + end1,
                     end1 - start1, edbuf + start1);
    input_replace(start1, end2 - start1, buf, n);
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
    if (pos <= edstart || (pos == edlen && prev_cell(pos) <= edstart))
        return;

    /* swap the cells [i, j) and [j, k) */
    int j, k;
    if (pos < edlen) {
        j = pos;
        k = next_cell(pos);
    } else {
        k = pos;
        j = prev_cell(pos);
    }
    int i = prev_cell(j);

    char buf[2 * BUFSIZE];
    memcpy(buf, edbuf + j, k - j);
    memcpy(buf + k - j, edbuf + i, j - i);
    input_replace(i, k - i, buf, k - i);
}

/*
 * undo and redo: copies of the input line and the cursor before each
 * change; while reading a character name, its own lists are used
 */
enum { UNDO_MAX = 256 };

struct undo_step {
    char *line;
    int pos;
};

struct undo_list {
    struct undo_step steps[UNDO_MAX];
    int count;
};

static struct undo_lists {
    struct undo_list undo, redo;
} line_undo, name_undo, *undos = &line_undo;

static bool searching(void);

/* the line before the command that runs, and whether to compare it after */
static struct {
    char line[BUFSIZE];
    int pos;
    struct undo_lists *lists;   /* NULL if not recording */
    bool typing;                /* characters are being typed */
} undo_cmd;

static void undo_push(struct undo_list *list, const char *line, int cursor)
{
    char *copy = xstrdup(line);
    if (list->count == UNDO_MAX) {
        free(list->steps[0].line);
        memmove(list->steps, list->steps + 1,
                (UNDO_MAX - 1) * sizeof(*list->steps));
        --list->count;
    }
    list->steps[list->count++] = (struct undo_step){ .line = copy,
                                                     .pos = cursor };
}

static void undo_clear(struct undo_list *list)
{
    while (list->count > 0)
        free(list->steps[--list->count].line);
}

/* record 'line' with the cursor at 'cursor' as the line before a change */
static void undo_record(const char *line, int cursor)
{
    undo_push(&undos->undo, line, cursor);
    undo_clear(&undos->redo);
}

/*
 * before a command bound to a key runs: keep the line, to record it if
 * the command changes it; not while searching, which records its result
 */
void undo_before(void)
{
    undo_cmd.typing = false;
    undo_cmd.lists = searching() ? NULL : undos;
    strcpy(undo_cmd.line, edbuf);
    undo_cmd.pos = pos;
}

/*
 * after the command 'funct', which 'ran' unless a mode handled the key
 * instead: record the line if it changed it
 */
void undo_after(function_str funct, bool ran)
{
    if (ran && funct == enter_line) {
        undo_clear(&line_undo.undo);
        undo_clear(&line_undo.redo);
        return;
    }
    if (undo_cmd.lists != undos || funct == undo || funct == redo)
        return;                 /* a mode started or ended, or not recording */
    if (strcmp(undo_cmd.line, edbuf) != 0)
        undo_record(undo_cmd.line, undo_cmd.pos);
}

/* before a character is typed: record the line at the start of typing */
static void undo_typing(void)
{
    if (!undo_cmd.typing) {
        undo_cmd.typing = true;
        undo_record(edbuf, pos);
    }
}

/*
 * edit the line of the last step of 'from' again, keeping the line as it
 * is in 'to'
 */
static void undo_move(struct undo_list *from, struct undo_list *to)
{
    if (from->count == 0) {
        tty_putc(ASCII_BEL);
        return;
    }
    struct undo_step step = from->steps[--from->count];
    undo_push(to, edbuf, pos);
    input_replace(0, edlen, step.line, strlen(step.line));
    input_moveto(step.pos);
    free(step.line);
    undo_cmd.typing = false;
}

/*
 * undo the last change of the input line, like undo in readline
 */
void undo(char *dummy)
{
    undo_move(&undos->undo, &undos->redo);
}

/*
 * redo the last change undone, like undo-redo in Emacs
 */
void redo(char *dummy)
{
    undo_move(&undos->redo, &undos->undo);
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
    char *s = xmalloc(old_len + len + 1);
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
    kill_left(pos - edstart);
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
    edlen = pos;
    edbuf[edlen] = '\0';
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
    if (pos > edstart) {
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
    if (edlen == 0)
        return;
    clear_input_line(false);
    put_history(edbuf);
    pickline = curline;
    *edbuf = '\0';
    edlen = 0;
    pos = 0;
}

/*
 * put string in history at current position, unless it contains a secret
 * (string is assumed to be trashable)
 */
void put_history(char *str)
{
    if (secret_in(str))
        return;
    if (hist[curline] != NULL)
        free(hist[curline]);
    hist[curline] = xstrdup(str);

    if (++curline == MAX_HIST)
        curline = 0;

    /* split into words and put into completion list */
    for (char *p = strtok(str, DELIM); p != NULL; p = strtok(NULL, DELIM)) {
        /* no commands/short words */
        if (strlen(p) >= MIN_WORDLEN && p[0] != '#')
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
    words[i].next = wordindex;
    words[i].prev = words[wordindex].prev;
    words[words[wordindex].prev].next = i;
    words[wordindex].prev = i;
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
 * completing the names of commands and their arguments
 */

/* the words that the word being typed can be completed to */
struct word_list {
    int count, size;
    char **words;
};

static void word_list_add(struct word_list *l, const char *word)
{
    for (int i = 0; i < l->count; ++i) {
        if (strcmp(l->words[i], word) == 0)
            return;
    }
    if (l->count == l->size) {
        int size = l->size > 0 ? 2 * l->size : 32;
        l->words = xrealloc(l->words, size * sizeof(*l->words));
        l->size = size;
    }
    l->words[l->count++] = xstrdup(word);
}

static void word_list_free(struct word_list *l)
{
    for (int i = 0; i < l->count; ++i)
        free(l->words[i]);
    free(l->words);
}

/* the topics of #help: the entries of the help file */
static void complete_help(struct word_list *l, int arg, const char *args,
                          const char *word)
{
    size_t flag_len = strcspn(args, " ");
    if (arg == 0) {
        word_list_add(l, "-P");
        word_list_add(l, "--no-pager");
    } else if (arg == 1
               && ((flag_len == 2 && strncmp(args, "-P", 2) == 0)
                   || (flag_len == 10
                       && strncmp(args, "--no-pager", 10) == 0))) {
        arg = 0;                /* the topic, after the flag */
    }
    if (arg != 0)
        return;
    word_list_add(l, "copyright");      /* not in the help file */
    FILE *f = fopen(helpfile, "r");
    if (f == NULL)
        return;
    char line[BUFSIZE];
    while (fgets(line, sizeof(line), f) != NULL) {
        line[strcspn(line, "\n")] = '\0';
        if (line[0] == '@' && isalpha((unsigned char)line[1]))
            word_list_add(l, line + 1);
    }
    fclose(f);
}

/* add the words of 'list', which ends with NULL */
static void add_words(struct word_list *l, const char *const *list)
{
    for (; *list != NULL; ++list)
        word_list_add(l, *list);
}

/* on or off, for #quote */
static void complete_on_off(struct word_list *l, int arg, const char *args,
                            const char *word)
{
    static const char *const names[] = { "on", "off", NULL };
    if (arg == 0)
        add_words(l, names);
}

/* the flag of #ver */
static void complete_ver(struct word_list *l, int arg, const char *args,
                         const char *word)
{
    static const char *const names[] = { "-v", NULL };
    if (arg == 0)
        add_words(l, names);
}

/* the subcommands of #secret */
static void complete_secret(struct word_list *l, int arg, const char *args,
                            const char *word)
{
    static const char *const names[] = { "add", "delete", NULL };
    if (arg == 0)
        add_words(l, names);
}

/* what #request sends, any number of them */
static void complete_request(struct word_list *l, int arg, const char *args,
                             const char *word)
{
    static const char *const names[] = { "all", "editor", NULL };
    add_words(l, names);
}

/* the variables of #setvar */
static void complete_setvar(struct word_list *l, int arg, const char *args,
                            const char *word)
{
    static const char *const names[] = {
        "buffer", "lines", "mem", "timer", NULL
    };
    if (arg == 0)
        add_words(l, names);
}

/* the line-editing functions, for #keyedit */
static void complete_keyedit(struct word_list *l, int arg, const char *args,
                             const char *word)
{
    if (arg != 0)
        return;
    for (int i = 1; internal_functions[i].name != NULL; ++i)
        word_list_add(l, internal_functions[i].name);
}

/* the labels of the delayed commands, active or not, for #in and #at */
static void complete_delay(struct word_list *l, int arg, const char *args,
                           const char *word)
{
    if (arg != 0)
        return;
    for (const delaynode *p = delays; p != NULL; p = p->next)
        word_list_add(l, p->name);
    for (const delaynode *p = dead_delays; p != NULL; p = p->next)
        word_list_add(l, p->name);
}

/* the ids of the open connections, for #snoop and #zap */
static void complete_session(struct word_list *l, int arg, const char *args,
                             const char *word)
{
    if (arg != 0)
        return;
    for (int i = 0; i < MAX_CONNECTS; ++i) {
        if (CONN_INDEX(i).id != NULL)
            word_list_add(l, CONN_INDEX(i).id);
    }
}

/* the flags of #connect and #host, wherever a word starts with - */
static void complete_connect(struct word_list *l, int arg, const char *args,
                             const char *word)
{
    static const char *const names[] = {
        "--tls", "--tls=no-verify", NO_TLS_FLAG, UTF8_FLAG, LATIN1_FLAG,
        NULL
    };
    if (word[0] == '-')
        add_words(l, names);
}

/* the names of the keys bound */
static void add_key_names(struct word_list *l)
{
    for (const keynode *k = keydefs; k != NULL; k = k->next)
        word_list_add(l, k->name);
}

/* the arguments of #bind: edit, functions, or the name of a key */
static void complete_bind(struct word_list *l, int arg, const char *args,
                          const char *word)
{
    if (arg != 0)
        return;
    word_list_add(l, "edit");
    word_list_add(l, "functions");
    add_key_names(l);
}

/* the name of a key, as the first argument */
static void complete_key(struct word_list *l, int arg, const char *args,
                         const char *word)
{
    if (arg == 0)
        add_key_names(l);
}

/* the lists of #reset, and the keys after bind */
static void complete_reset(struct word_list *l, int arg, const char *args,
                           const char *word)
{
    static const char *const lists[] = {
        "action", "alias", "all", "at", "bind", "in", "mark", "prompt",
        "substitute", "var",
    };
    if (arg == 0) {
        for (size_t i = 0; i < sizeof(lists) / sizeof(*lists); ++i)
            word_list_add(l, lists[i]);
    } else if (arg == 1 && strncmp(args, "bind ", 5) == 0) {
        add_key_names(l);
    }
}

static void add_word(const char *word, void *data)
{
    word_list_add(data, word);
}

/* the arguments of #hilite: input or messages, and then an attribute */
static void complete_hilite(struct word_list *l, int arg, const char *args,
                            const char *word)
{
    size_t len = strcspn(args, " ");
    if ((len == 5 && strncmp(args, "input", 5) == 0)
        || (len == 8 && strncmp(args, "messages", 8) == 0)) {
        args += len;
    } else if (arg == 0) {
        word_list_add(l, "input");
        word_list_add(l, "messages");
    }
    attr_next_words(args, add_word, l);
}

/* the modes of #delim */
static void complete_delim(struct word_list *l, int arg, const char *args,
                           const char *word)
{
    if (arg != 0)
        return;
    for (int i = 0; i < DELIM_MODES; ++i)
        word_list_add(l, delim_name[i]);
}

/* the options of #option, after +, - or =, and list */
static void complete_option(struct word_list *l, int arg, const char *args,
                            const char *word)
{
    for (int i = 0; option_name(i) != NULL; ++i)
        word_list_add(l, option_name(i));
    for (int i = 0; command_option_name(i) != NULL; ++i) {
        char name[BUFSIZE];
        snprintf(name, sizeof(name), "%s=", command_option_name(i));
        word_list_add(l, name);
    }
    word_list_add(l, "list");
}

/* the groups of the aliases and actions */
static void add_group_names(struct word_list *l)
{
    for (const aliasnode *p = sortedaliases; p != NULL; p = p->snext) {
        if (p->group != NULL)
            word_list_add(l, p->group);
    }
    for (const actionnode *p = actions; p != NULL; p = p->next) {
        if (p->group != NULL)
            word_list_add(l, p->group);
    }
}

/* the arguments of #group: the name of a group, and on, off or list */
static void complete_group(struct word_list *l, int arg, const char *args,
                           const char *word)
{
    if (arg == 0) {
        add_group_names(l);
    } else if (arg == 1) {
        word_list_add(l, "on");
        word_list_add(l, "off");
        word_list_add(l, "list");
    }
}

/* the names of the aliases, for #alias */
static void complete_alias(struct word_list *l, int arg, const char *args,
                           const char *word)
{
    if (arg != 0)
        return;
    for (const aliasnode *p = sortedaliases; p != NULL; p = p->snext)
        word_list_add(l, p->name);
}

/* the labels of the actions, for #action after any of <=>% and + or - */
static void complete_action(struct word_list *l, int arg, const char *args,
                            const char *word)
{
    if (arg != 0)
        return;
    for (const actionnode *p = actions; p != NULL; p = p->next) {
        if (p->label != NULL && p->label[0] != '\0')
            word_list_add(l, p->label);
    }
}

/* the labels of the prompts, for #prompt as for #action */
static void complete_prompt(struct word_list *l, int arg, const char *args,
                            const char *word)
{
    if (arg != 0)
        return;
    for (const promptnode *p = prompts; p != NULL; p = p->next) {
        if (p->label != NULL && p->label[0] != '\0')
            word_list_add(l, p->label);
    }
}

/* the patterns of the marks without spaces, for #mark */
static void complete_mark(struct word_list *l, int arg, const char *args,
                          const char *word)
{
    if (arg != 0)
        return;
    for (const marknode *p = markers; p != NULL; p = p->next) {
        if (strchr(p->b.pattern, ' ') == NULL)
            word_list_add(l, p->b.pattern);
    }
}

/* the named variables, as @name and $name, for #var */
static void complete_var(struct word_list *l, int arg, const char *args,
                         const char *word)
{
    if (arg != 0)
        return;
    static const char signs[VAR_KINDS] = {
        [VAR_NUM] = '@', [VAR_TEXT] = '$'
    };
    for (enum var_kind kind = VAR_NUM; kind < VAR_KINDS; ++kind) {
        for (const varnode *v = named_vars[kind].sorted; v != NULL;
             v = v->snext) {
            char name[BUFSIZE];
            snprintf(name, sizeof(name), "%c%s", signs[kind], v->name);
            word_list_add(l, name);
        }
    }
}

/*
 * the files in the directory of 'word', with that directory before them,
 * and directories with a / after them; those starting with a dot only if
 * the name typed does
 */
static void complete_file(struct word_list *l, int arg, const char *args,
                          const char *word)
{
    if (arg != 0)
        return;
    const char *slash = strrchr(word, '/');
    const char *base = slash != NULL ? slash + 1 : word;
    int dir_len = slash != NULL ? (int)(slash - word) + 1 : 0;
    char dir[BUFSIZE];
    snprintf(dir, sizeof(dir), "%.*s", dir_len, word);
    DIR *d = opendir(dir_len > 0 ? dir : ".");
    if (d == NULL)
        return;
    for (const struct dirent *e = readdir(d); e != NULL; e = readdir(d)) {
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0
            || (e->d_name[0] == '.' && base[0] != '.'))
            continue;
        char path[BUFSIZE];
        if (snprintf(path, sizeof(path), "%s%s", dir, e->d_name)
            >= (int)sizeof(path))
            continue;
        struct stat st;
        bool is_dir = stat(path, &st) == 0 && S_ISDIR(st.st_mode);
        if (is_dir && strlen(path) + 1 < sizeof(path))
            strcat(path, "/");
        word_list_add(l, path);
    }
    closedir(d);
}

/*
 * how the arguments of commands are completed: a function that adds the
 * words that argument 'arg' can be, after the arguments 'args' before it,
 * where 'word' is what is typed of it;
 * the characters of 'prefix' at the start of the word, such as + before
 * an option, are not part of it
 */
static const struct {
    const char *command;
    void (*complete)(struct word_list *l, int arg, const char *args,
                     const char *word);
    const char *prefix;
} arg_completions[] = {
    { "action", complete_action, "<=>%+-" },
    { "alias", complete_alias, "" },
    { "at", complete_delay, "" },
    { "bind", complete_bind, "" },
    { "capture", complete_file, ">" },
    { "connect", complete_connect, "" },
    { "delim", complete_delim, "" },
    { "file", complete_file, "=" },
    { "group", complete_group, "" },
    { "help", complete_help, "" },
    { "hilite", complete_hilite, "" },
    { "host", complete_connect, "" },
    { "in", complete_delay, "" },
    { "key", complete_key, "" },
    { "keyedit", complete_keyedit, "" },
    { "load", complete_file, "" },
    { "mark", complete_mark, "" },
    { "movie", complete_file, "" },
    { "option", complete_option, "+-=" },
    { "prompt", complete_prompt, "<=>%+-" },
    { "quote", complete_on_off, "" },
    { "rebind", complete_key, "" },
    { "record", complete_file, "" },
    { "request", complete_request, "" },
    { "reset", complete_reset, "" },
    { "save", complete_file, "" },
    { "secret", complete_secret, "" },
    { "setvar", complete_setvar, "" },
    { "snoop", complete_session, "" },
    { "var", complete_var, "" },
    { "ver", complete_ver, "" },
    { "zap", complete_session, "" },
};

/*
 * where the name of a file starts in the word at 'word', of a command
 * 'name' that reads one after a <, as #send <file and #var $x=<file, or -1
 */
static int redirect_file(const char *name, int word)
{
    static const char *const readers[] = {
        "emulate", "exe", "print", "send", "var"
    };
    size_t i = 0;
    while (i < sizeof(readers) / sizeof(*readers)
           && strcmp(readers[i], name) != 0)
        ++i;
    if (i == sizeof(readers) / sizeof(*readers))
        return -1;
    int p = word;
    if (strcmp(name, "var") == 0) {
        while (p < pos && edbuf[p] != '=')
            ++p;
        p = p < pos ? p + 1 : word;
    }
    return p < pos && edbuf[p] == '<' ? p + 1 : -1;
}

/* the command that 'name' runs, as its start, or NULL */
static const cmdstruct *find_command(const char *name, int len)
{
    for (const cmdstruct *c = commands; c != NULL; c = c->next) {
        if (len > 0 && strncmp(name, c->name, len) == 0 && c->funct != NULL)
            return c;
    }
    return NULL;
}

/*
 * list the words of 'l' that start with the 'len' bytes of 'word' above the
 * input line, in columns, down and then across, without their first 'skip'
 * bytes, such as the directory of file names; 'l' is sorted, so those words
 * follow each other
 */
static void word_list_show(const struct word_list *l, const char *word,
                           int len, int skip)
{
    int first = 0;
    while (first < l->count && strncmp(l->words[first], word, len) != 0)
        ++first;
    int count = 0, width = 0;
    while (first + count < l->count
           && strncmp(l->words[first + count], word, len) == 0) {
        int w = (int)strlen(l->words[first + count]) - skip;
        if (w > width)
            width = w;
        ++count;
    }
    int columns = cols / (width + 2);
    if (columns < 1)
        columns = 1;
    int rows = (count + columns - 1) / columns;
    clear_input_line(false);
    tty_putc('\n');
    for (int row = 0; row < rows; ++row) {
        char line[BUFSIZE];
        int n = 0;
        for (int i = row; i < count && n < (int)sizeof(line); i += rows) {
            n += snprintf(line + n, sizeof(line) - n, "%-*s", width + 2,
                          l->words[first + i] + skip);
        }
        if (n >= (int)sizeof(line))
            n = (int)sizeof(line) - 1;
        while (n > 0 && line[n - 1] == ' ')
            --n;
        line[n] = '\0';
        tty_message("%s\n", line);
    }
    status(1);
}

static int compare_words(const void *a, const void *b)
{
    return strcmp(*(char *const *)a, *(char *const *)b);
}

/*
 * complete the name of a command, or its argument, like readline: to the
 * only word that matches, and a space, or as far as those that match have
 * in common; if that adds nothing, beep, and list them if Tab is pressed
 * again; return false if the cursor is not in a command that can be
 * completed
 */
static bool complete_command(void)
{
    static bool tabbed;
    if (last_edit_cmd != complete_word)
        tabbed = false;

    /* the command the cursor is in, after any ; or { before it */
    int start = pos;
    while (start > 0 && edbuf[start - 1] != ';' && edbuf[start - 1] != '{')
        --start;
    while (start < pos && edbuf[start] == ' ')
        ++start;
    if (edbuf[start] != '#' || edbuf[start + 1] == '#')
        return false;

    /* the word being typed, and the arguments before it */
    int word = pos;
    while (word > start && edbuf[word - 1] != ' ')
        --word;
    struct word_list l = { 0 };
    bool file_names = false;
    bool last = false;          /* nothing follows the word, not a space */
    if (word == start) {
        for (const cmdstruct *c = commands; c != NULL; c = c->next) {
            if (c->funct != NULL && isalpha((unsigned char)c->name[0])) {
                char name[64];
                snprintf(name, sizeof(name), "#%s", c->name);
                word_list_add(&l, name);
            }
        }
    } else {
        int name_len = strcspn(edbuf + start + 1, " ");
        const cmdstruct *c = find_command(edbuf + start + 1, name_len);
        if (c == NULL)
            return false;
        int file = redirect_file(c->name, word);
        if (file >= 0) {
            word = file;
            char typed[BUFSIZE];
            snprintf(typed, sizeof(typed), "%.*s", pos - word, edbuf + word);
            complete_file(&l, 0, "", typed);
            file_names = true;
            last = true;
        } else {
            size_t i = 0;
            while (i < sizeof(arg_completions) / sizeof(*arg_completions)
                   && strcmp(arg_completions[i].command, c->name) != 0)
                ++i;
            if (i == sizeof(arg_completions) / sizeof(*arg_completions))
                return false;

            int args = start + 1 + name_len;
            while (args < word && edbuf[args] == ' ')
                ++args;
            int arg = 0;
            for (int p = args; p < word; ++p) {
                if (edbuf[p] == ' ' && edbuf[p + 1] != ' ')
                    ++arg;
            }
            char before[BUFSIZE];
            snprintf(before, sizeof(before), "%.*s", word - args,
                     edbuf + args);
            while (word < pos && edbuf[word] != '\0'
                   && strchr(arg_completions[i].prefix, edbuf[word]) != NULL)
                ++word;
            char typed[BUFSIZE];
            snprintf(typed, sizeof(typed), "%.*s", pos - word, edbuf + word);
            arg_completions[i].complete(&l, arg, before, typed);
            file_names = arg_completions[i].complete == complete_file;
        }
    }
    if (l.count > 0)            /* else words is NULL, which qsort rejects */
        qsort(l.words, l.count, sizeof(*l.words), compare_words);

    /* the words that start with the word being typed */
    int len = pos - word;
    const char *only = NULL;
    char common[BUFSIZE] = "";
    int matches = 0;
    for (int i = 0; i < l.count; ++i) {
        const char *w = l.words[i];
        if (strncmp(w, edbuf + word, len) != 0)
            continue;
        if (matches++ == 0) {
            only = w;
            snprintf(common, sizeof(common), "%s", w);
        } else {
            size_t j = 0;
            while (common[j] != '\0' && common[j] == w[j])
                ++j;
            common[j] = '\0';
        }
    }
    if (matches == 1) {
        /* a space after it, unless it is a directory, to go on in it, an
         * option that a command follows, as editor=, or the file after a
         * <, which a space would be part of */
        char text[BUFSIZE];
        size_t only_len = strlen(only);
        bool go_on = (last
                      || (only_len > 0
                          && (only[only_len - 1] == '/'
                              || only[only_len - 1] == '=')));
        snprintf(text, sizeof(text), go_on ? "%s" : "%s ", only);
        input_replace(word, len, text, strlen(text));
        tabbed = false;
    } else if (matches > 1 && (int)strlen(common) > len) {
        /* more than one still matches: list them on the next Tab, as
         * readline does */
        input_replace(word, len, common, strlen(common));
        tabbed = true;
    } else if (matches > 1 && tabbed) {
        /* file names without their directory, which they all have */
        int skip = 0;
        for (int j = 0; file_names && j < len; ++j) {
            if (edbuf[word + j] == '/')
                skip = j + 1;
        }
        word_list_show(&l, edbuf + word, len, skip);
    } else {
        tabbed = true;
        tty_putc(ASCII_BEL);
    }
    word_list_free(&l);
    return true;
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

    if (complete_command()) {
        comp_len = 0;
        return;
    }

    /* find word start */
    int k, n;
    if (last_edit_cmd == complete_word && comp_len != 0) {
        k = comp_len;
        input_moveto(pos - k);
        n = pos - root_len;
    } else {
        n = pos;
        while (n > 0 && !IS_DELIM(edbuf[n - 1]))
            --n;
        k = 0;
        curr_word = wordindex;
        root_len = pos - n;
    }
    char *root = edbuf + n;
    comp_len = 0;

    /* k = chars to delete,  n = position of starting word */

    /* scan word list for next match */
    char *p;
    for (;;) {
        curr_word = compl_next_word(curr_word);
        p = compl_get_word(curr_word);
        if (p == NULL)
            break;
        if (strncasecmp(p, root, root_len) != 0 || p[root_len] == '\0')
            continue;
        p += root_len;
        n = strlen(p);
        if (n + edlen < BUFSIZE) {
            comp_len = n;
            input_replace(pos, k, p, n);
            k = 0;
            break;
        }
    }
    if (k > 0)
        input_delete_nofollow_chars(k);

    /* delete duplicate instances of the word */
    if (p != NULL && curr_word >= 0
        && !(words[curr_word].flags & WORD_UNIQUE)) {
        words[curr_word].flags |= WORD_UNIQUE;
        p = words[curr_word].word;
        n = words[curr_word].next;
        while (words[n].word != NULL) {
            k = n;
            n = words[k].next;
            if (strcmp(p, words[k].word) == 0) {
                demote_word(k);
                free(words[k].word);
                words[k].word = NULL;
                /* move retain flag */
                words[curr_word].flags |= words[k].flags;
                words[k].flags &= WORD_UNIQUE;
                if (words[k].flags != 0)
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
    static int curr_line = MAX_HIST - 1, root_len = 0, first_line = 0;

    if (last_edit_cmd != complete_line) {
        root_len = edlen;
        curr_line = curline;
        first_line = curline;
    }

    int i;
    for (i = curr_line - 1; i != curr_line; --i) {
        if (i < 0)
            i = MAX_HIST - 1;
        if (i == first_line)
            break;
        if (hist[i] != NULL && strncmp(edbuf, hist[i], root_len) == 0)
            break;
    }
    if (i != curr_line) {
        clear_input_line(false);
        if (i == first_line) {
            edbuf[root_len] = '\0';
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
    /* TODO: add some way to handle new commands going in the default
     * completions list */
    char buf[BUFSIZE] = "#";
    for (cmdstruct *p = commands; p != NULL; p = p->next) {
        if (p->funct != NULL) {
            strcpy(buf + 1, p->name);
            put_static_word(buf);
        }
    }
    /* init 'words' double-linked list */
    for (int i = MAX_WORDS - 1; i >= 0; --i) {
        words[i].prev = i - 1;
        words[i].next = i + 1;
    }
    words[0].prev = MAX_WORDS - 1;
    words[MAX_WORDS - 1].next = 0;
}

void put_static_word(char *s)
{
    if (secret_in(s))
        return;
    if (static_words.used >= static_words.size) {
        int size = static_words.size;
        do {
            size = size != 0 ? size * 2 : 16;
        } while (static_words.used >= size);
        static_words.words = xrealloc(static_words.words,
                                      sizeof(static_words.words[0]) * size);
        static_words.size = size;
    }
    static_words.words[static_words.used++] = xstrdup(s);
}

/*
 * put word in word completion ring, unless it contains a secret
 */
void put_word(char *s)
{
    if (secret_in(s))
        return;
    int r = wordindex;
    words[r].word = xstrdup(s);
    words[r].flags = 0;
    r = words[r].prev;
    demote_word(r);
    wordindex = r;
    if (words[r].word != NULL) {
        free(words[r].word);
        words[r].word = NULL;
    }
}

/*
 * delete the history lines and the completion words that contain a
 * secret; the other history lines keep their order, before the line
 * being typed
 */
void purge_secrets(void)
{
    int to = curline;
    int from = curline;
    for (int n = 0; n < MAX_HIST; ++n) {
        from = (from + MAX_HIST - 1) % MAX_HIST;
        char *line = hist[from];
        if (line == NULL)
            continue;
        hist[from] = NULL;
        if (secret_in(line)) {
            free(line);
        } else {
            to = (to + MAX_HIST - 1) % MAX_HIST;
            hist[to] = line;
        }
    }
    pickline = curline;

    for (int i = 0; i < MAX_WORDS; ++i) {
        if (words[i].word != NULL && secret_in(words[i].word)) {
            free(words[i].word);
            words[i].word = NULL;
            /* the empty words are the last, before wordindex */
            demote_word(i);
        }
    }

    int kept = 0;
    for (int i = 0; i < static_words.used; ++i) {
        if (secret_in(static_words.words[i]))
            free(static_words.words[i]);
        else
            static_words.words[kept++] = static_words.words[i];
    }
    static_words.used = kept;
}

/*
 * set delimeters[DELIM_CUSTOM]
 */
void set_custom_delimeters(char *s)
{
    static char *custom;
    free(custom);
    custom = xstrdup(s);
    delim_list[DELIM_CUSTOM] = custom;
    delim_len[DELIM_CUSTOM] = strlen(s);
    delim_mode = DELIM_CUSTOM;
}

void fmt_next_edit(const char *fmt, ...)
{
    va_list va;
    va_start(va, fmt);
    int n = vsnprintf(inserted_next, sizeof(inserted_next), fmt, va);
    if (n < 0 || (size_t)n >= sizeof(inserted_next)) {
        static const char msg[] = " [TRUNCATED]";
        memcpy(inserted_next + sizeof(inserted_next) - sizeof(msg),
               msg, sizeof(msg));
    }
    va_end(va);
}

/*
 * enter a line
 */
void enter_line(char *dummy)
{
    if (line_status == 0) {
        input_moveto(edlen);
    } else {
        if (prompt_status != 0)
            col0 = 0;
        draw_input_line();
    }
    message_printf("%s\n", edattrend);

    line0 = CURLINE(edlen);
    if (line0 < lines - 1)
        ++line0;

    log_record_line(edbuf);

    col0 = 0;
    error = 0;
    pos = 0;
    line_status = 0;

    if (*edbuf == '\0' || (verbatim && *edbuf != '#'))
        tcp_write(current_connection, edbuf);
    else
        parse_user_input(edbuf, true);
    history_done = 0;

    /* don't put identical lines in history, nor empty ones */
    char *p = hist[curline != 0 ? curline - 1 : MAX_HIST - 1];
    if (p == NULL || (edlen > 0 && strcmp(edbuf, p) != 0))
        put_history(edbuf);
    pickline = curline;
    if (*inserted_next != '\0') {
        strcpy(edbuf, inserted_next);
        inserted_next[0] = '\0';
        line_status = 1;
    } else if (*prefixstr != '\0') {
        strcpy(edbuf, prefixstr);
        line_status = 1;
    } else {
        edbuf[0] = '\0';
    }
    edlen = strlen(edbuf);
    pos = edlen;
}

/*
 * move one word forward
 */
void next_word(char *dummy)
{
    int i = pos;
    while (i < edlen && !word_at(i))
        i = next_cell(i);
    while (i < edlen && word_at(i))
        i = next_cell(i);
    input_moveto(i);
}

/*
 * move one word backward
 */
void prev_word(char *dummy)
{
    int i = pos;
    while (i > edstart && !word_at(prev_cell(i)))
        i = prev_cell(i);
    while (i > edstart && word_at(prev_cell(i)))
        i = prev_cell(i);
    input_moveto(i);
}

/*
 * delete word to the right
 */
void del_word_right(char *dummy)
{
    int i = pos;
    while (i < edlen && !word_at(i))
        i = next_cell(i);
    while (i < edlen && word_at(i))
        i = next_cell(i);
    kill_right(i - pos);
}

/*
 * delete word to the left
 */
void del_word_left(char *dummy)
{
    int i = pos;
    while (i > edstart && !word_at(prev_cell(i)))
        i = prev_cell(i);
    while (i > edstart && word_at(prev_cell(i)))
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
    while (i > edstart && (edbuf[i - 1] == ' ' || edbuf[i - 1] == '\t'))
        --i;
    while (i > edstart && edbuf[i - 1] != ' ' && edbuf[i - 1] != '\t')
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
    return cp <= ASCII_MAX ? (uint32_t)toupper(cp) : cp;
#endif
}

static uint32_t to_lower(uint32_t cp)
{
#ifdef USE_LOCALE
    return towlower(cp);
#else
    return cp <= ASCII_MAX ? (uint32_t)tolower(cp) : cp;
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
    return cp <= ASCII_MAX && isalpha(cp);
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
 * edit history line 'i', keeping the line edited in its place in the
 * history, so that it can be edited again
 */
/*
 * keep 'line', edited from history line 'i', in its place; a line with a
 * secret is dropped, leaving the history line as it was, or the line
 * being typed empty
 */
static void keep_history_edit(int i, const char *line)
{
    if (secret_in(line)) {
        if (i != curline)
            return;
        line = "";
    }
    if (hist[i] == NULL || strcmp(hist[i], line) != 0) {
        free(hist[i]);
        hist[i] = xstrdup(line);
    }
}

static void goto_history_line(int i)
{
    keep_history_edit(pickline, edbuf);
    pickline = i;
    clear_input_line(false);
    strcpy(edbuf, hist[pickline]);
    edlen = strlen(edbuf);
    pos = edlen;
}

/*
 * get previous line from history list
 */
void prev_line(char *dummy)
{
    int i = pickline - 1;
    if (i < 0)
        i = MAX_HIST - 1;
    if (hist[i] != NULL)
        goto_history_line(i);
}

/*
 * get next line from history list
 */
void next_line(char *dummy)
{
    int i = pickline + 1;
    if (i == MAX_HIST)
        i = 0;
    if (hist[i] != NULL)
        goto_history_line(i);
}

/*
 * get the first line of the history list, like beginning-of-history
 */
void begin_of_history(char *dummy)
{
    int i = pickline;
    for (;;) {
        int prev = (i + MAX_HIST - 1) % MAX_HIST;
        if (hist[prev] == NULL || prev == curline)
            break;
        i = prev;
    }
    if (i != pickline)
        goto_history_line(i);
}

/*
 * get the line being typed, after the last line of the history list
 */
void end_of_history(char *dummy)
{
    if (pickline != curline && hist[curline] != NULL)
        goto_history_line(curline);
}

/* where the last word inserted by M-. is, and from which history line */
static struct {
    int index, start;
} yank_word;

/*
 * insert the last word of the previous line in the history; again right
 * after, replace it with the last word of the line before, like
 * yank-last-arg in readline
 */
void yank_last_word(char *dummy)
{
    bool again = last_edit_cmd == yank_last_word && yank_word.start <= pos;
    int i = again ? yank_word.index : curline;
    for (int n = 0; n < MAX_HIST; ++n) {
        i = (i + MAX_HIST - 1) % MAX_HIST;
        if (hist[i] == NULL || i == curline)
            break;
        /* the last word, separated by spaces */
        const char *line = hist[i];
        int end = strlen(line);
        while (end > 0 && (line[end - 1] == ' ' || line[end - 1] == '\t'))
            --end;
        int start = end;
        while (start > 0 && line[start - 1] != ' ' && line[start - 1] != '\t')
            --start;
        if (start == end)
            continue;
        if (again) {
            input_replace(yank_word.start, pos - yank_word.start,
                          line + start, end - start);
        } else {
            yank_word.start = pos;
            input_insert_follow_chars((char *)line + start, end - start);
        }
        yank_word.index = i;
        return;
    }
    tty_putc(ASCII_BEL);
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

static bool searching(void)
{
    return isearch.active;
}

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
    edlabel = n < (int)sizeof(buf) ? n : (int)sizeof(buf) - 1;
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
            keep_history_edit(isearch.origin, isearch.saved);
            pickline = isearch.index;
            line = hist[pickline];
        }
    }
    if (accept && strcmp(line, isearch.saved) != 0)
        undo_record(isearch.saved, isearch.saved_pos);
    edlabel = 0;
    input_replace(0, edlen, line, strlen(line));
    input_moveto(new_pos);
}

static void isearch_start_or_repeat(bool reverse)
{
    if (!isearch.active) {
        isearch.active = true;
        isearch.reverse = reverse;
        isearch.failed = false;
        isearch.query_len = 0;
        isearch.query[0] = '\0';
        isearch.origin = pickline;
        isearch.index = pickline;
        strcpy(isearch.saved, edbuf);
        isearch.saved_pos = pos;
        isearch.offset = pos;
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
    isearch.query_len += n;
    isearch.query[isearch.query_len] = '\0';
    isearch_find(isearch.index, isearch.offset);
    isearch_show();
}

/* delete the last character of the query, and search again */
static void isearch_del_char(void)
{
    if (isearch.query_len == 0)
        return;
    int n = (utf8_prev_cell(isearch.query, isearch.query + isearch.query_len)
             - isearch.query);
    isearch.query_len = n;
    isearch.query[n] = '\0';
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
static bool isearch_command(function_str funct)
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
 * reading the name of a character to insert, like insert-char in Emacs:
 * the input line shows a prompt and the name, which can be edited, and the
 * line being edited is in 'saved'
 */
static struct {
    bool active;
    bool tabbed;                /* Tab completed nothing, list on the next */
    const char *file;           /* the names, of characters or emoji */
    /* the text, as last taken from the line: for emoji, it can be the
     * group, a slash, the subgroup, a slash and the name, of which 'parts'
     * are typed: 0 if only the name, 1 if the group and subgroup, or 2 */
    char text[BUFSIZE / 2];
    int parts;
    char group[64], subgroup[64];
    char name[BUFSIZE / 2];
    int name_at;                /* where the name starts in the text */
    char saved[BUFSIZE];
    int saved_pos;
} charin;

/* the names that Tab lists at most */
enum { CHARIN_LIST_MAX = 20 };

/* take the text from the input line, and its parts */
static void charin_get_name(void)
{
    int len = edlen - edstart;
    if (len >= (int)sizeof(charin.text))
        len = sizeof(charin.text) - 1;
    memcpy(charin.text, edbuf + edstart, len);
    charin.text[len] = '\0';

    charin.parts = 0;
    charin.group[0] = '\0';
    charin.subgroup[0] = '\0';
    charin.name_at = 0;
    const char *slash = (charin.file == emojifile
                         ? strchr(charin.text, '/') : NULL);
    if (slash != NULL) {
        snprintf(charin.group, sizeof(charin.group), "%.*s",
                 (int)(slash - charin.text), charin.text);
        const char *sub = slash + 1;
        slash = strchr(sub, '/');
        charin.parts = slash != NULL ? 2 : 1;
        snprintf(charin.subgroup, sizeof(charin.subgroup), "%.*s",
                 (int)(slash != NULL ? slash - sub : (int)strlen(sub)), sub);
        charin.name_at = (slash != NULL
                          ? slash + 1 - charin.text
                          : (int)strlen(charin.text));
    }
    strcpy(charin.name, charin.text + charin.name_at);
}

/* replace the name in the input line with 'name', after the group */
static void charin_set_name(const char *name)
{
    input_replace(edstart + charin.name_at, edlen - edstart - charin.name_at,
                  name, strlen(name));
}

/*
 * replace the name in the input line with the first 'keep' bytes of it,
 * which are kept, and then 'text'; return where 'text' starts
 */
static int charin_set_name_after(size_t keep, const char *text)
{
    bool space = (keep > 0 && !charname_is_sep(charin.name[keep - 1])
                  && text[0] != '\0');
    char name[sizeof(charin.name)];
    snprintf(name, sizeof(name), "%.*s%s%s", (int)keep, charin.name,
             space ? " " : "", text);
    charin_set_name(name);
    return (int)keep + (space ? 1 : 0);
}

/* replace all the text in the input line with 'text' */
static void charin_set_text(const char *text)
{
    input_replace(edstart, edlen - edstart, text, strlen(text));
}

/* find the names that match the name being read, in its group */
static int charin_find(charname_fn fn, void *data)
{
    if (charin.parts == 2)
        return charname_find(charin.file, charin.group, charin.subgroup,
                             charin.name, fn, data);
    return charname_find(charin.file, NULL, NULL, charin.name, fn, data);
}

/* the groups and subgroups of the emoji */
struct group_list {
    int count;
    struct group_entry {
        char group[64], subgroup[64];
        int count;
    } entries[128];
};

static void group_list_add(const char *group, const char *subgroup, int count,
                           void *data)
{
    struct group_list *l = data;
    if (l->count < (int)(sizeof(l->entries) / sizeof(*l->entries))) {
        struct group_entry *e = &l->entries[l->count++];
        snprintf(e->group, sizeof(e->group), "%s", group);
        snprintf(e->subgroup, sizeof(e->subgroup), "%s", subgroup);
        e->count = count;
    }
}

/* stop reading the name, and edit the line as it was */
static void charin_end(void)
{
    charin.active = false;
    undo_clear(&name_undo.undo);
    undo_clear(&name_undo.redo);
    undos = &line_undo;
    edstart = 0;
    edlabel = 0;
    input_replace(0, edlen, charin.saved, strlen(charin.saved));
    input_moveto(charin.saved_pos);
}

/* start reading a name in 'file' after 'label' */
static void charin_start(const char *file, const char *label)
{
    if (charin.active)
        return;
    charin.active = true;
    charin.tabbed = false;
    charin.file = file;
    strcpy(charin.saved, edbuf);
    charin.saved_pos = pos;
    edlabel = (int)strlen(label);
    input_replace(0, edlen, label, strlen(label));
    edstart = edlen;
    undos = &name_undo;
}

/*
 * a label of powwow shown instead of the input line while reading what
 * is not shown, as a secret for #secret; it is shown by the main loop,
 * as the command that asks for it may still be reading edbuf
 */
static struct {
    const char *pending;        /* the label to show, or NULL */
    bool shown;
    char saved[BUFSIZE];        /* the input line it replaces */
    int saved_pos;
} input_label;

void input_label_start(const char *label)
{
    input_label.pending = label;
}

void input_label_show(void)
{
    if (input_label.pending == NULL || input_label.shown)
        return;
    clear_input_line(false);
    strcpy(input_label.saved, edbuf);
    input_label.saved_pos = pos;
    strcpy(edbuf, input_label.pending);
    edlen = edlabel = pos = (int)strlen(edbuf);
    line_status = 1;
    input_label.shown = true;
}

void input_label_end(void)
{
    input_label.pending = NULL;
    if (!input_label.shown)
        return;
    input_label.shown = false;
    clear_input_line(false);
    strcpy(edbuf, input_label.saved);
    edlen = (int)strlen(edbuf);
    pos = input_label.saved_pos;
    edlabel = 0;
    line_status = 1;
}

/*
 * a line typed without being shown: a secret for #secret, or a line in
 * no-echo mode, where the MUD echoes, such as a password; the keys go
 * through the key bindings, but only those that end, cancel or delete
 * the line do anything, and control characters are not typed
 */
static char hidden_buf[BUFSIZE];
static size_t hidden_len;

bool hidden_typing(void)
{
    return (secret_reading()
            || (linemode & (LM_NOECHO | LM_CHAR)) == LM_NOECHO);
}

/* clear the line typed, deleted characters too, even if not read again */
static void hidden_clear(void)
{
    volatile char *v = hidden_buf;
    for (size_t i = 0; i < sizeof(hidden_buf); ++i)
        v[i] = '\0';
    hidden_len = 0;
}

void hidden_insert(char c)
{
    if ((unsigned char)c < ' ' || c == ASCII_DEL)
        tty_putc(ASCII_BEL);
    else if (hidden_len < sizeof(hidden_buf) - 1)
        hidden_buf[hidden_len++] = c;
}

void hidden_command(function_str funct)
{
    if (funct == enter_line) {
        if (secret_reading()) {
            secret_entered(hidden_buf, hidden_len);
        } else {
            if (edattrbg)
                tty_puts(edattrend);
            tcp_write(current_connection, hidden_buf);
        }
        hidden_clear();
    } else if (funct == abort_edit) {
        hidden_clear();
        if (secret_reading())
            secret_entered(hidden_buf, 0);
    } else if (funct == del_char_left) {
        while (hidden_len > 0
               && utf8_is_continuation(hidden_buf[--hidden_len]))
            hidden_buf[hidden_len] = '\0';
        hidden_buf[hidden_len] = '\0';
    } else if (funct == kill_to_bol) {
        hidden_clear();
    } else {
        tty_putc(ASCII_BEL);
    }
}

/*
 * read the name or code point of a character, and insert it
 */
void insert_named_char(char *dummy)
{
    charin_start(namesfile, "(insert-char) name or hex: ");
}

/*
 * read the name of an emoji, and insert it
 */
void insert_emoji(char *dummy)
{
    charin_start(emojifile, "(insert-emoji) name: ");
}

/*
 * how well 'name' matches 'pattern', the name being read or its words
 * after those of the group: 0 if it starts with it, 1 if it starts with
 * its first word, or else 2
 */
static int charin_tier(const char *pattern, const char *name)
{
    if (charname_starts_with(pattern, name))
        return 0;
    if (charname_words_match(pattern, name, true))
        return 1;
    return 2;
}

/* some of the names that match: how many, the first, and their common start */
struct name_set {
    int count;
    char first[BUFSIZE / 2];
    char common[BUFSIZE / 2];
};

static void name_set_add(struct name_set *set, const char *name)
{
    if (set->count++ == 0) {
        snprintf(set->first, sizeof(set->first), "%s", name);
        snprintf(set->common, sizeof(set->common), "%s", name);
    } else {
        size_t i = 0;
        while (set->common[i] != '\0' && set->common[i] == name[i])
            ++i;
        set->common[i] = '\0';
    }
}

/*
 * the names that the name being read matches: all of them, those that
 * start with it, and those that start with its first word, also aligned
 * with its words; those whose words it abbreviates, one by one; its
 * words are those from byte 'keep' on, and 'name_start' is where those
 * start that match all the names by themselves, after the words of the
 * group of an emoji
 */
struct charin_matches {
    struct name_set all, prefixed, anchored, abbreviated;
    struct charname_align all_words, prefixed_words, anchored_words;
    size_t keep, name_start;
};

static void charin_match(const char *name, const uint32_t *cps, int ncps,
                         const char *group, void *data)
{
    struct charin_matches *m = data;
    const char *pattern = charin.name + m->keep;
    int tier = charin_tier(pattern, name);
    name_set_add(&m->all, name);
    charname_align_add(&m->all_words, pattern, name);
    if (charname_words_abbreviate(pattern, name))
        name_set_add(&m->abbreviated, name);
    if (group != NULL) {
        size_t start = charname_name_start(charin.name, name);
        if (start > m->name_start)
            m->name_start = start;
    }
    if (tier == 0) {
        name_set_add(&m->prefixed, name);
        charname_align_add(&m->prefixed_words, pattern, name);
    }
    if (tier <= 1) {
        name_set_add(&m->anchored, name);
        charname_align_add(&m->anchored_words, pattern, name);
    }
}

/* what the name being read can be completed to, from the names in 'set' */
static const char *name_set_completion(const struct name_set *set)
{
    return set->count == 1 ? set->first : set->common;
}

/* the names that match, to list them */
struct charin_listing {
    int count, size;
    struct listed_name {
        int tier;
        char *name;
        char *shown;            /* with its group, if it matched with it */
        char *chars;            /* the character or emoji itself */
    } *names;
};

static void charin_list_add(const char *name, const uint32_t *cps, int ncps,
                            const char *group, void *data)
{
    struct charin_listing *l = data;
    if (l->count == l->size) {
        int size = l->size > 0 ? 2 * l->size : 64;
        l->names = xrealloc(l->names, size * sizeof(*l->names));
        l->size = size;
    }
    char shown[256], chars[CHARNAME_MAX * UTF8_MAX + 1];
    snprintf(shown, sizeof(shown), group != NULL ? "%s (%s)" : "%s",
             name, group);
    int n = 0;
    for (int i = 0; i < ncps; ++i)
        n += utf8_put(cps[i], chars + n);
    chars[n] = '\0';
    /* an emoji that matches with its group, by the words of its name */
    const char *pattern = (group != NULL
                           ? charin.name + charname_name_start(charin.name,
                                                               name)
                           : charin.name);
    l->names[l->count++] = (struct listed_name){
        .tier = charin_tier(pattern, name), .name = xstrdup(name),
        .shown = xstrdup(shown), .chars = xstrdup(chars)
    };
}

/* the best match first: by tier, then the shortest, then alphabetically */
static int compare_listed(const void *a, const void *b)
{
    const struct listed_name *x = a, *y = b;
    if (x->tier != y->tier)
        return x->tier - y->tier;
    size_t xlen = strlen(x->name), ylen = strlen(y->name);
    if (xlen != ylen)
        return xlen < ylen ? -1 : 1;
    return strcmp(x->name, y->name);
}

/*
 * list the groups of emoji that match the text being read: the subgroups
 * of the group typed before a slash, or else the groups that the words
 * typed match, or the subgroups that they match
 */
static void charin_list_groups(void)
{
    struct group_list l = { 0 };
    charname_groups(charin.file, group_list_add, &l);
    for (int i = 0; i < l.count; ++i) {
        const struct group_entry *e = &l.entries[i];
        bool first = i == 0 || strcmp(e->group, l.entries[i - 1].group) != 0;
        if (charin.parts == 1) {
            if (charname_words_match(charin.group, e->group, false)
                && charname_words_match(charin.subgroup, e->subgroup,
                                        false)) {
                tty_message("%s/%s/ (%d emoji)\n", e->group, e->subgroup,
                            e->count);
            }
        } else if (charname_words_match(charin.name, e->group, false)) {
            if (first) {
                int count = 0;
                for (int j = i;
                     j < l.count && strcmp(l.entries[j].group, e->group) == 0;
                     ++j)
                    count += l.entries[j].count;
                tty_message("%s/ (%d emoji)\n", e->group, count);
            }
        } else if (charname_words_match(charin.name, e->subgroup, false)) {
            tty_message("%s/%s/ (%d emoji)\n", e->group, e->subgroup,
                        e->count);
        }
    }
}

/*
 * list the names that match above the input line, the best ones first,
 * after the groups of emoji that match
 */
static void charin_list(void)
{
    clear_input_line(false);
    tty_putc('\n');
    if (charin.file == emojifile && charin.parts < 2)
        charin_list_groups();
    if (charin.parts == 1) {
        status(1);
        return;
    }

    struct charin_listing l = { 0 };
    charin_find(charin_list_add, &l);
    if (l.count > 0)            /* else names is NULL, which qsort rejects */
        qsort(l.names, l.count, sizeof(*l.names), compare_listed);

    /* the characters after the names, lined up, as their width varies
     * with the terminal */
    int width = 0;
    for (int i = 0; i < l.count && i < CHARIN_LIST_MAX; ++i) {
        int w = utf8_width(l.names[i].shown, strlen(l.names[i].shown));
        if (w > width)
            width = w;
    }
    for (int i = 0; i < l.count; ++i) {
        if (i < CHARIN_LIST_MAX) {
            tty_message("%s%*s  %s\n", l.names[i].shown,
                        width - utf8_width(l.names[i].shown,
                                           strlen(l.names[i].shown)), "",
                        l.names[i].chars);
        }
        free(l.names[i].name);
        free(l.names[i].shown);
        free(l.names[i].chars);
    }
    if (l.count > CHARIN_LIST_MAX)
        tty_message("#and %d more.\n", l.count - CHARIN_LIST_MAX);
    free(l.names);
    status(1);
}

/* Tab completed nothing: beep, and list the names if pressed again */
static void charin_no_completion(void)
{
    if (!charin.tabbed) {
        charin.tabbed = true;
        tty_putc(ASCII_BEL);
    } else {
        charin_list();
    }
}

/*
 * complete the group and subgroup of emoji typed before a slash: the
 * group, if only one matches, and the subgroup, as far as those that
 * match have in common
 */
static void charin_complete_group(void)
{
    struct group_list l = { 0 };
    charname_groups(charin.file, group_list_add, &l);
    struct name_set groups = { 0 }, subgroups = { 0 };
    for (int i = 0; i < l.count; ++i) {
        const struct group_entry *e = &l.entries[i];
        if (!charname_words_match(charin.group, e->group, false)
            || !charname_words_match(charin.subgroup, e->subgroup, false))
            continue;
        if (groups.count == 0 || strcmp(groups.first, e->group) != 0)
            name_set_add(&groups, e->group);
        name_set_add(&subgroups, e->subgroup);
    }
    if (groups.count != 1) {
        if (subgroups.count == 0)
            tty_putc(ASCII_BEL);
        else
            charin_no_completion();
        return;
    }
    const char *sub = charin.subgroup;
    if (subgroups.count == 1)
        sub = subgroups.first;
    else if (strlen(subgroups.common) > strlen(sub)
             && charname_starts_with(sub, subgroups.common))
        sub = subgroups.common;
    char text[sizeof(charin.text)];
    int len = snprintf(text, sizeof(text), "%s/%s%s", groups.first, sub,
                       subgroups.count == 1 ? "/" : "");
    if (len < (int)sizeof(text) && strcmp(text, charin.text) != 0) {
        charin_set_text(text);
        charin.tabbed = false;
    } else {
        charin_no_completion();
    }
}

/*
 * complete the name, like readline: to the only name that matches, or to
 * the start that those that match have in common, preferring those that
 * start with it, and then those that start with its first word; if that
 * adds nothing, beep, and list them if Tab is pressed again
 */
static void charin_complete(void)
{
    charin_get_name();
    if (charin.parts == 1) {
        charin_complete_group();
        return;
    }
    struct charin_matches *m = xcalloc(1, sizeof(*m));
    if (charin_find(charin_match, m) <= 0) {
        free(m);
        tty_putc(ASCII_BEL);
        return;
    }
    /* the words typed that match the group of the emoji stay, and the
     * words after them are completed with the names */
    if (m->name_start > 0) {
        *m = (struct charin_matches){ .keep = m->name_start };
        charin_find(charin_match, m);
    }
    const char *pattern = charin.name + m->keep;
    /* the names that start with the name typed, or else with its first
     * word, are the ones meant, if any */
    const struct name_set *set = &m->all;
    const struct charname_align *align = &m->all_words;
    if (m->all.count > 1 && m->prefixed.count > 0) {
        set = &m->prefixed;
        align = &m->prefixed_words;
    } else if (m->all.count > 1 && m->anchored.count > 0) {
        set = &m->anchored;
        align = &m->anchored_words;
    }
    /* the words typed as the whole words they stand for, and the cursor
     * where the names differ, as "latin  letter aa" for "lati letter aa",
     * between "latin small letter aa" and "latin capital letter aa" */
    char text[sizeof(charin.name)];
    size_t cursor;
    if (set->count > 1
        && charname_align_text(align, pattern, text, sizeof(text),
                               &cursor)) {
        int at = edstart + charin.name_at + (int)m->keep;
        if (strcmp(text, pattern) != 0 || at + (int)cursor != pos) {
            /* more than one still matches: list them on the next Tab,
             * as readline does */
            at = (edstart + charin.name_at
                  + charin_set_name_after(m->keep, text));
            input_moveto(at + (int)cursor);
            charin.tabbed = true;
        } else {
            charin_no_completion();
        }
        free(m);
        return;
    }
    const char *longer = name_set_completion(set);
    if (strlen(longer) > strlen(pattern)
        && charname_words_match(pattern, longer, false)) {
        charin_set_name_after(m->keep, longer);
        charin.tabbed = set->count > 1;
    } else {
        charin_no_completion();
    }
    free(m);
}

/*
 * insert the character named, or the only one whose name matches, as for
 * Tab, or the only one whose words the words typed abbreviate, as "lat sma
 * let dz" does "latin small letter dz", or else beep
 */
static void charin_insert(void)
{
    uint32_t cps[CHARNAME_MAX];
    charin_get_name();
    int n = charname_lookup(charin.file, charin.name, strlen(charin.name),
                            cps);
    if (n == 0) {
        struct charin_matches m = { 0 };
        const char *name = NULL;
        if (charin_find(charin_match, &m) == 1) {
            name = m.all.first;
        } else if (m.abbreviated.count == 1) {
            name = m.abbreviated.first;
        } else if (m.name_start > 0) {
            /* the name after the words of the group, as Tab leaves it */
            name = charin.name + m.name_start;
        }
        if (name != NULL)
            n = charname_lookup(charin.file, name, strlen(name), cps);
    }
    if (n == 0) {
        tty_putc(ASCII_BEL);
        return;
    }
    charin_end();
    undo_record(charin.saved, charin.saved_pos);
    char buf[CHARNAME_MAX * UTF8_MAX];
    int len = 0;
    for (int i = 0; i < n; ++i)
        len += utf8_put(cps[i], buf + len);
    input_insert_follow_chars(buf, len);
}

/* can 'funct' edit the name being read? */
static bool charin_can_edit(function_str funct)
{
    static const function_str editing[] = {
        prev_char, next_char, begin_of_line, end_of_line, prev_word,
        next_word, del_char_left, del_char_right, del_word_left,
        del_word_right, del_unix_word_left, kill_to_eol, kill_to_bol, yank,
        yank_pop, transpose_chars, upcase_word, downcase_word,
        capitalize_word, redraw_line, insert_string, undo, redo,
    };
    for (size_t i = 0; i < sizeof(editing) / sizeof(*editing); ++i) {
        if (funct == editing[i])
            return true;
    }
    return false;
}

/*
 * while reading or searching, handle the command 'funct' bound to a key;
 * return true if 'funct' was handled here and must not run
 */
bool edit_mode_command(function_str funct)
{
    if (!charin.active)
        return isearch_command(funct);
    if (funct != complete_word)
        charin.tabbed = false;
    if (funct == complete_word)
        charin_complete();
    else if (funct == enter_line)
        charin_insert();
    else if (funct == abort_edit)
        charin_end();
    else if (charin_can_edit(funct))
        return false;
    else
        tty_putc(ASCII_BEL);
    return true;
}

/*
 * cancel any search or reading, before the input line is used without
 * editing
 */
void edit_mode_cancel(void)
{
    if (isearch.active)
        isearch_end(false);
    if (charin.active)
        charin_end();
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
    if (line_status != 0)
        return;
    int lev = 0;
    int i;
    for (i = pos - 1; i >= 0; --i) {
        if (is_rparen(edbuf[i])) {
            ++lev;
        } else if (is_lparen(edbuf[i])) {
            --lev;
            if (lev == 0) {
                if (matching_lparen(c) == edbuf[i])
                    break;
                else
                    i = -1;
            }
        }
    }
    if (i >= 0) {
        tty_gotoxy_opt(CURCOL(pos), CURLINE(pos), CURCOL(i), CURLINE(i));
        flashback = true;
        excursion = i;
    }
}

/*
 * put cursor back where it belongs
 */
void putbackcursor(void)
{
    if (line_status == 0)
        tty_gotoxy_opt(CURCOL(excursion), CURLINE(excursion), CURCOL(pos),
                       CURLINE(pos));
    flashback = false;
}

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
    if (flashback)
        putbackcursor();
    input_insert_follow_chars(insbuf, ninsbuf);
    ninsbuf = 0;
}

/* insert typed character (byte) 'c' before the next insert_flush() */
void insert_char(char c)
{
    static char pend[UTF8_MAX];
    static int npend, need;
    unsigned char b = c;

    if (npend > 0 && utf8_is_continuation(b)) {
        pend[npend++] = b;
        if (npend < need)
            return;
    } else {
        npend = 0;
        if (b > ASCII_MAX) {
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
    if (utf8_get(pend, n, &cp) != n || unicode_is_c1(cp))
        return;                 /* invalid or C1 control */

    if (isearch.active) {
        isearch_add(pend, n);
        return;
    }
    if (charin.active)
        charin.tabbed = false;
    undo_typing();
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
    if (arg == NULL || *arg == '\0')
        return;

    char buf[BUFSIZE];
    my_strncpy(buf, arg, BUFSIZE - 1);
    unescape(buf);
    int len = strlen(buf);

    if (len > 1) {
        if (flashback)
            putbackcursor();
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
    } else if (!opt_compact) {
        tty_putc('\n');
    }

    status(1);
    error = 0;

    log_record_line(edbuf);

    parse_instruction(cmd, true, false, true);
    history_done = 0;
}

void edit_bootstrap(void)
{
    default_completions();
}
