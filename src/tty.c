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

#include <langinfo.h>
#include <locale.h>
#include <strings.h>
#include <wchar.h>

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

#ifdef USE_VT100 /* hard-coded vt100 features if no termcap: */

/* terminal escape sequences */
static const char kpadstart[] = "";
static const char kpadend[] = "";
static const char begoln[] = "\r";
static const char clreoln[] = "\033[K";
static const char clreoscr[] = "\033[J";
static const char leftcur[] = "\033[D";
static const char upcur[] = "\033[A";
static const char cursor_left[] = "\033[D";
static const char cursor_right[] = "\033[C";
static const char cursor_up[] = "\033[A";
static const char cursor_down[] = "\033[B";
static const char cursor_delete[] = "\033[3~";
static const char cursor_home[] = "\033[H";
static const char cursor_end[] = "\033[F";

/* attribute changers */
static const char modebold[] = "\033[1m";
static const char modeblink[] = "\033[5m";
static const char modeinv[] = "\033[7m";
static const char modeuline[] = "\033[4m";
static const char modestandon[] = "";
static const char modestandoff[] = "";
static const char modenorm[] = "\033[m";

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
static char leftcur[CAPLEN], upcur[CAPLEN];
static char curgoto[CAPLEN];
static char cursor_left[CAPLEN], cursor_right[CAPLEN];
static char cursor_up[CAPLEN], cursor_down[CAPLEN];
static char cursor_delete[CAPLEN];
static char cursor_home[CAPLEN], cursor_end[CAPLEN];

/* attribute changers */
static char modebold[CAPLEN], modeblink[CAPLEN], modeinv[CAPLEN];
static char modeuline[CAPLEN], modestandon[CAPLEN], modestandoff[CAPLEN];
static char modenorm[CAPLEN];

static int len_begoln, len_leftcur, len_upcur, gotocost;

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
static bool wrapglitch = false;

static FILE *tty_read_stream;
static int orig_read_fd_fl;
static struct {
    mbstate_t mbstate;              /* multibyte output shift state */
    char      data[4096];           /* buffer for pending data */
    size_t    used;                 /* bytes used of data */
    int       fd;                   /* file descriptor to write to */
    bool      utf8;                 /* the locale uses UTF-8 */
    bool      input;                /* drawing the input line */
    char      pend[UTF8_MAX];       /* incomplete UTF-8 character */
    int       npend, need;          /* bytes in pend, and expected */
} tty_write_state = {
    .fd = 1                         /* also before tty_start() */
};

/* tell tty_putc whether it is drawing the input line */
static void tty_set_input(bool input)
{
    tty_write_state.input = input;
}

static struct termios ttybsave;
static bool tty_started;        /* ttybsave holds the state to restore */

/*
 * the name of the interrupt key of the terminal, as C-c, which stops the
 * running command and, pressed again, quits powwow
 */
const char *tty_intr_key(void)
{
    static char name[8];
    unsigned char c = ttybsave.c_cc[VINTR];
    if (c == _POSIX_VDISABLE)
        return "the interrupt key";
    if (c < ' ')
        snprintf(name, sizeof(name), "C-%c", c + '`');
    else if (c == ASCII_DEL)
        return "Backspace";
    else
        snprintf(name, sizeof(name), "%c", c);
    return name;
}

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

/* does the terminal take UTF-8, as the locale says? */
bool tty_locale_utf8(void)
{
    const char *codeset = nl_langinfo(CODESET);
    return (strcasecmp(codeset, "UTF-8") == 0
            || strcasecmp(codeset, "UTF8") == 0);
}

/*
 * Set the terminal to character-at-a-time-without-echo mode, and save the
 * original state in ttybsave
 */
void tty_start(void)
{
    tcgetattr(tty_read_fd, &ttybsave);
    tty_started = true;
    struct termios ttyb = ttybsave;
    ttyb.c_lflag &= ~(ECHO | ICANON);
    /* let ^S and ^Q through instead of stopping and starting the output,
     * and Ret (^M) and LF (^J) through as they are, like readline */
    ttyb.c_iflag &= ~(IXON | ICRNL | INLCR);
    ttyb.c_cc[VTIME] = 0;
    ttyb.c_cc[VMIN] = 1;
    disable_special_keys(&ttyb);
    tcsetattr(tty_read_fd, TCSANOW, &ttyb);

    orig_read_fd_fl = fcntl(tty_read_fd, F_GETFL);
    fcntl(tty_read_fd, F_SETFL, O_NONBLOCK | orig_read_fd_fl);

    tty_puts(kpadstart);
    tty_flush();

    tty_write_state.utf8 = tty_locale_utf8();
    if (wcrtomb(NULL, L'\0', &tty_write_state.mbstate) == (size_t)-1) {
        perror("wcrtomb()");
        abort();
    }
}

/*
 * Reset the terminal to its original state
 */
void tty_quit(void)
{
    /* the terminal is as powwow found it before tty_start(), such as
     * when a fatal error ends powwow while it starts */
    if (!tty_started)
        return;
    tty_started = false;
    tcsetattr(tty_read_fd, TCSANOW, &ttybsave);
    tty_puts(kpadend);
    tty_flush();
    fcntl(tty_read_fd, F_SETFL, orig_read_fd_fl);
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
    /* if ioctl fails or gives silly values, don't change anything */
    struct winsize wsiz;
    if (ioctl(tty_read_fd, TIOCGWINSZ, &wsiz) != 0 || wsiz.ws_row == 0
        || wsiz.ws_col == 0)
        return;
    /*
     * a narrower window is drawn as if it were MIN_COLS wide: the input
     * line needs rows of at least 2 columns, for a wide character, and
     * without the wrap glitch the last column is not used
     */
    enum { MIN_COLS = 3 };
    int ws_cols = wsiz.ws_col < MIN_COLS ? MIN_COLS : wsiz.ws_col;
    if (lines != wsiz.ws_row || cols != ws_cols) {
        lines = wsiz.ws_row;
        cols = ws_cols;
        cols_1 = ws_cols;
        if (!wrapglitch)
            --cols_1;

        if (main_connection != NULL)
            tcp_write_tty_size(main_connection);
        line0 += lines - olines;

        tty_gotoxy(0, line0);
        /* so we know where the cursor is */
        if (edattrbg)
            tty_printf("%s%s", edattrend, tty_clreoscr);
        else
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
    tty_read_stream = stdin;
    /* already for the banner, before tty_start() */
    tty_write_state.utf8 = tty_locale_utf8();

#ifdef USE_VT100
    cols = 80;
    lines = 24;
#else   /* not USE_VT100 */
    struct tc_init_node {
        char cap[4];
        char *buf;
        int *len;
        bool critic;
    };
    static struct tc_init_node tc_init[] = {
        { .cap = "cm", .buf = curgoto, .critic = true },
        { .cap = "ce", .buf = clreoln, .critic = true },
        { .cap = "cd", .buf = clreoscr, .critic = true },
        { .cap = "le", .buf = leftcur, .len = &len_leftcur },
        { .cap = "up", .buf = upcur, .len = &len_upcur },
        { .cap = "cr", .buf = begoln, .len = &len_begoln },
        { .cap = "me", .buf = modenorm },
        { .cap = "md", .buf = modebold },
        { .cap = "mb", .buf = modeblink },
        { .cap = "mr", .buf = modeinv },
        { .cap = "us", .buf = modeuline },
        { .cap = "so", .buf = modestandon },
        { .cap = "se", .buf = modestandoff },
        { .cap = "ks", .buf = kpadstart },
        { .cap = "ke", .buf = kpadend },
        { .cap = "kl", .buf = cursor_left },
        { .cap = "kr", .buf = cursor_right },
        { .cap = "ku", .buf = cursor_up },
        { .cap = "kd", .buf = cursor_down },
        { .cap = "kD", .buf = cursor_delete },
        { .cap = "kh", .buf = cursor_home },
        { .cap = "@7", .buf = cursor_end },
        { .cap = "" }
    };
    char *term = getenv("TERM");
    if (term == NULL) {
        fprintf(stderr, "$TERM not set\n");
        exit(1);
    }
    char tcbuf[2048];           /* by convention, this is enough */
    switch (tgetent(tcbuf, term)) {
    case 1:
        break;
    case 0:
        fprintf(stderr,
                "There is no entry for \"%s\" in the terminal data"
                " base.\n", term);
        fprintf(stderr,
                "Please set your $TERM environment variable correctly.\n");
        exit(1);
    default:
        syserr("cannot read the terminal database");
    }
    for (struct tc_init_node *np = tc_init; np->cap[0] != '\0'; ++np) {
        int len = extract(np->cap, np->buf);
        if (len != 0) {
            if (np->len != NULL)
                *np->len += len;
        } else if (np->critic) {
            fprintf(stderr,
                    "Your \"%s\" terminal is not powerful enough, missing"
                    " \"%s\".\n", term, np->cap);
            exit(1);
        }
    }
    if (len_begoln == 0) {
        strcpy(begoln, "\r");
        len_begoln = 1;
    }
    if (len_leftcur == 0) {
        strcpy(leftcur, "\b");
        len_leftcur = 1;
    }

    gotocost = strlen(tgoto(curgoto, cols - 1, lines - 1));

    /* this must be before getting window size */
    wrapglitch = tgetflag("xn");
    /* for the default size, kept if the window has it or has none */
    cols_1 = wrapglitch ? cols : cols - 1;

    tty_sig_winch_bottomhalf(); /* get window size */
#endif  /* not USE_VT100 */
}

/*
 * the default keypad keys, which walk around; termcap does not have their
 * sequences, so they are those of a vt100 (they can be changed with #bind
 * or #rebind)
 */
static const default_bind walk_binds[] = {
    { .label = "KP2", .seq = "\033Or",
      .funct = key_run_command, .command = "s" },
    { .label = "KP3", .seq = "\033Os",
      .funct = key_run_command, .command = "d" },
    { .label = "KP4", .seq = "\033Ot",
      .funct = key_run_command, .command = "w" },
    { .label = "KP5", .seq = "\033Ou",
      .funct = key_run_command, .command = "exits" },
    { .label = "KP6", .seq = "\033Ov",
      .funct = key_run_command, .command = "e" },
    { .label = "KP7", .seq = "\033Ow",
      .funct = key_run_command, .command = "look" },
    { .label = "KP8", .seq = "\033Ox",
      .funct = key_run_command, .command = "n" },
    { .label = "KP9", .seq = "\033Oy",
      .funct = key_run_command, .command = "u" },
};

void tty_add_walk_binds(void)
{
    for (size_t i = 0; i < sizeof(walk_binds) / sizeof(*walk_binds); ++i)
        add_keynode(walk_binds[i].label, walk_binds[i].seq, 0,
                    walk_binds[i].funct, walk_binds[i].command);
}

/*
 * the default editing keys, with the save-file version that added them
 * (see tty_add_new_binds); the cursor keys and Delete are bound only if
 * termcap has their sequences
 */
static const default_bind default_binds[] = {
    { .label = "LF", .seq = "\n", .funct = enter_line },
    { .label = "Ret", .seq = "\r", .funct = enter_line },
    { .label = "C-h", .seq = "\b", .funct = del_char_left },
    { .label = "Backspace", .seq = "\177", .funct = del_char_left },
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
    { .label = "C-x-8-Ret", .seq = "\030" "8\r",
      .funct = insert_named_char,
      .since = READLINE_KEYS_SAVEFILEVER },
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
    { .label = "M-Backspace", .seq = "\033\177", .funct = del_word_left },
    { .label = "Left", .seq = cursor_left, .funct = prev_char },
    { .label = "Right", .seq = cursor_right, .funct = next_char },
    { .label = "Up", .seq = cursor_up, .funct = prev_line },
    { .label = "Down", .seq = cursor_down, .funct = next_line },
    { .label = "Delete", .seq = cursor_delete, .funct = del_char_right,
      .since = READLINE_KEYS_SAVEFILEVER },
    { .label = "Home", .seq = cursor_home, .funct = begin_of_line,
      .since = READLINE_KEYS_SAVEFILEVER },
    { .label = "End", .seq = cursor_end, .funct = end_of_line,
      .since = READLINE_KEYS_SAVEFILEVER },
    { .label = "M-<", .seq = "\033<", .funct = begin_of_history,
      .since = READLINE_KEYS_SAVEFILEVER },
    { .label = "M->", .seq = "\033>", .funct = end_of_history,
      .since = READLINE_KEYS_SAVEFILEVER },
    { .label = "M-.", .seq = "\033.", .funct = yank_last_word,
      .since = READLINE_KEYS_SAVEFILEVER },
    { .label = "C-_", .seq = "\037", .funct = undo,
      .since = READLINE_KEYS_SAVEFILEVER },
    { .label = "C-x-C-u", .seq = "\030\025", .funct = undo,
      .since = READLINE_KEYS_SAVEFILEVER },
    { .label = "M-C-_", .seq = "\033\037", .funct = redo,
      .since = READLINE_KEYS_SAVEFILEVER },
    { .label = "C-x-8-e-e", .seq = "\030" "8ee", .funct = insert_emoji,
      .since = READLINE_KEYS_SAVEFILEVER },
    { .label = "C-x-8-e-s", .seq = "\030" "8es", .funct = insert_emoji,
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
 * is no key 'new' yet, and add it to the list 'renamed', of 'size'
 */
static void rename_key(const char *old, const char *seq, const char *new,
                       char *renamed, size_t size)
{
    keynode *k = *lookup_key(old);
    int len = strlen(seq);
    if (k == NULL || k->seqlen != len || memcmp(k->sequence, seq, len) != 0
        || *lookup_key(new) != NULL)
        return;
    add_keynode(new, k->sequence, k->seqlen, k->funct, k->call_data);
    delete_keynode(lookup_key(old));
    size_t n = strlen(renamed);
    snprintf(renamed + n, size - n, "%s%s to %s", n > 0 ? ", " : "", old,
             new);
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
        char renamed[BUFSIZE] = "";
        rename_key("BS", "\b", "C-h", renamed, sizeof(renamed));
        rename_key("M-BS", "\033\b", "M-C-h", renamed, sizeof(renamed));
        rename_key("Del", "\177", "Backspace", renamed, sizeof(renamed));
        rename_key("M-Del", "\033\177", "M-Backspace", renamed,
                   sizeof(renamed));
        if (renamed[0] != '\0')
            message_wrap(WRAP_CONT, "#keys renamed: %s", renamed);
    }

    char added[BUFSIZE] = "";
    size_t n = 0;
    for (size_t i = 0; i < sizeof(default_binds) / sizeof(*default_binds);
         ++i) {
        if (default_binds[i].since <= version
            || *default_binds[i].seq == '\0'
            || key_is_bound(default_binds[i].label, default_binds[i].seq))
            continue;
        add_keynode(default_binds[i].label, default_binds[i].seq, 0,
                    default_binds[i].funct, NULL);
        n += snprintf(added + n, sizeof(added) - n, "%s%s",
                      n > 0 ? ", " : "", default_binds[i].label);
        if (n >= sizeof(added))
            n = sizeof(added) - 1;
    }
    if (n > 0)
        message_wrap(WRAP_CONT, "#new default editing keys added: %s", added);

    keynode *k = *lookup_key("C-w");
    if (version < READLINE_KEYS_SAVEFILEVER && k != NULL
        && k->funct == to_history && k->seqlen == 1 && k->sequence[0] == '\027'
        && k->call_data == NULL) {
        k->funct = del_unix_word_left;
        message_wrap(WRAP_CONT,
                     "#C-w now deletes back to the previous space; M-q puts "
                     "the line in history");
    }
}

/*
 * find the default binding of the key 'label'; return NULL if it has none
 */
const default_bind *tty_default_bind(const char *label)
{
    for (size_t i = 0; i < sizeof(default_binds) / sizeof(*default_binds);
         ++i) {
        if (strcmp(default_binds[i].label, label) == 0
            && *default_binds[i].seq != '\0')
            return &default_binds[i];
    }
    for (size_t i = 0; i < sizeof(walk_binds) / sizeof(*walk_binds); ++i) {
        if (strcmp(walk_binds[i].label, label) == 0)
            return &walk_binds[i];
    }
    return NULL;
}

#ifndef USE_VT100
/*
 * extract termcap 'cap' and strcat it to buf.
 * return the length of the extracted string.
 */
static int extract(char *cap, char *buf)
{
    char *d = buf + strlen(buf);
    char *bp = d;
    char *s = tgetstr(cap, &bp);
    if (s == NULL) {
        *bp = '\0';
        return 0;
    }
    /*
     * Remove the padding information. We assume that no terminals
     * need padding nowadays. At least it makes things much easier.
     */
    s += strspn(s, "0123456789*");
    int len = 0;
    while (*s != '\0') {
        if (*s == '$' && s[1] == '<') {
            s = strchr(s, '>');
            if (s == NULL)
                break;
            ++s;
            continue;
        }
        *d++ = *s++;
        ++len;
    }
    *d = '\0';
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

    CLIP(fromline, 0, lines - 1);
    CLIP(toline, 0, lines - 1);

    /* First, move vertically to the correct line, then horizontally
     * to the right column. If this turns out to be fewer characters
     * than a direct cursor positioning (tty_gotoxy), use that.
     */
    for (;;) { /* gotoless */
        int cost = 0;
        int i = toline - fromline;
        if (i < 0) {
            if (len_upcur == 0)
                break;
            cost = -i * len_upcur;
            if (cost >= gotocost)
                break;
            do {
                strcpy(cp, upcur);
                cp += len_upcur;
            } while (++i != 0);
        } else if (i > 0) {
            cost = 2 * i;       /* lf is mapped to crlf on output */
            if (cost >= gotocost)
                break;
            do {
                *cp++ = '\n';
            } while (--i != 0);
            fromcol = 0;
        }
        i = tocol - fromcol;
        if (i < 0) {
            int dist = -i * len_leftcur;
            if (dist <= len_begoln + tocol) {
                cost += dist;
                if (cost > gotocost)
                    break;
                do {
                    strcpy(cp, leftcur);
                    cp += len_leftcur;
                } while (++i != 0);
            } else {
                cost += len_begoln;
                if (cost > gotocost)
                    break;
                strcpy(cp, begoln);
                cp += len_begoln;
                fromcol = 0;
                i = tocol;
            }
        }
        if (i != 0) {
            /* moving right would mean reprinting text whose width is
             * not one column per byte; just use tty_gotoxy */
            break;
        }
        *cp = '\0';
        tty_puts(buf);
        return;
    }
    tty_gotoxy(tocol, toline);
}

/*
 * does the cell of 'n' bytes at 's', 'w' columns wide, fit in a row
 * of the screen at column 'col'?
 */
bool tty_cell_fits(int col, const char *s, size_t n, int w)
{
    /*
     * Terminal.app draws only the first characters of a grapheme
     * cluster that ends in the last column, and the rest (a VS16, or
     * what follows a ZWJ) on the next row: keep those out of it
     */
    if (col > 0 && col + w == cols && n > (size_t)utf8_seqlen(*s))
        return false;
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
        size_t n = utf8_input_cell_len(s, end - s);
        int w = utf8_input_cell_width(s, n);
        if (!tty_cell_fits(c, s, n, w)) {
            /* does not fit; drawn on the next row */
            ++l;
            c = 0;
        }
        c += w;
        s += n;
    }
    if (c >= cols_1) {
        ++l;
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
        size_t n = utf8_input_cell_len(edbuf + p, edlen - p);
        if (!tty_cell_fits(*col, edbuf + p, n,
                           utf8_input_cell_width(edbuf + p, n))) {
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
    if (toline < 0) {
        line0 -= toline;
        toline = 0;
    } else if (toline > lines - 1) {
        line0 -= toline - lines + 1;
        toline = lines - 1;
    }
    if (absolute)
        tty_gotoxy(tocol, toline);
    else
        tty_gotoxy_opt(CURCOL(pos), fromline, tocol, toline);
}

/*
 * show 'hint' after the end of the input line, leaving the cursor where it
 * is, if it fits on the row; input_hide_hint() erases it
 */
void input_show_hint(const char *hint)
{
    if (line_status != 0)
        return;
    int line, col;
    input_layout_end(edlen, &line, &col);
    int width = (int)strlen(hint) + 1;
    if (col + width >= cols_1)
        return;
    input_cursor_to(line, col, false);
    tty_printf("%s %s%s%s%s", edattrend, msgattrbeg, hint, msgattrend,
               edattrbeg);
    int pline, pcol;
    input_layout(pos, &pline, &pcol);
    tty_gotoxy_opt(col + width, line, pcol, pline);
}

void input_hide_hint(void)
{
    if (line_status != 0)
        return;
    int line, col;
    input_layout_end(edlen, &line, &col);
    input_cursor_to(line, col, false);
    tty_printf("%s%s%s", edattrend, tty_clreoln, edattrbeg);
    int pline, pcol;
    input_layout(pos, &pline, &pcol);
    tty_gotoxy_opt(col, line, pcol, pline);
}

void input_moveto(int new_pos)
{
    /*
     * FEATURE: the line we are moving to might be less than 0, or greater
     * than lines - 1, if the display is too small to hold the whole
     * editline.  In that case, the input line should be (partially)
     * redrawn.
     */
    if (new_pos < edstart)
        new_pos = edstart;
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
    if (edattrbg)
        tty_printf("%s\n%s", edattrend, edattrbeg);
    else
        tty_putc('\n');
    if (++*line > lines - 1) {
        *line = lines - 1;
        --line0;
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
    tty_set_input(true);

    const char *s = edbuf + from, *end = edbuf + edlen;
    bool in_label = false;      /* drawing a label, as a message */
    while (s < end) {
        size_t n = utf8_input_cell_len(s, end - s);
        int w = utf8_input_cell_width(s, n);
        if (!tty_cell_fits(col, s, n, w)) {
            if (in_label) {
                tty_printf("%s%s", msgattrend, edattrbeg);
                in_label = false;
            }
            if (col < cols_1) {
                /* a wide character did not fit; blank the rest of
                 * the row, which may hold half of an old one (with
                 * spaces: see input_erase_below) */
                if (edattrbg)
                    tty_puts(edattrend);
                for (; col < cols_1; ++col)
                    tty_putc(' ');
                if (edattrbg)
                    tty_puts(edattrbeg);
            }
            input_newline(&line);
            col = 0;
        }
        if ((s - edbuf < edlabel) != in_label) {
            in_label = !in_label;
            if (in_label)
                tty_printf("%s%s", edattrend, msgattrbeg);
            else
                tty_printf("%s%s", msgattrend, edattrbeg);
        }
        /*
         * terminals do not agree on the width of a cluster of several
         * characters, nor of a character not shown, such as a zero
         * width space: put the cursor where the next cell goes
         */
        bool on_space = utf8_input_cell_on_space(s, n);
        bool resync = on_space || w == 0 || n > (size_t)utf8_seqlen(*s);
        if (on_space)
            tty_putc(' ');
        while (n-- > 0)
            tty_putc(*s++);
        col += w;
        if (resync && col < cols)
            tty_gotoxy(col, line);
    }
    tty_set_input(false);
    if (in_label)
        tty_printf("%s%s", msgattrend, edattrbeg);
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
    while (n > 0 && utf8_is_continuation(str[n]))
        --n;
    return n;
}

/*
 * where to redraw the input line from after changing it at byte offset
 * 'p': the start of the cell before 'p' if the change joined text to it,
 * such as a combining character, so that it must be redrawn too, or else
 * 'p'
 */
static int input_redraw_start(int p)
{
    int prev = utf8_prev_cell(edbuf, edbuf + p) - edbuf;
    if (prev == p)
        return p;
    int len = utf8_input_cell_len(edbuf + prev, edlen - prev);
    return prev + len > p ? prev : p;
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
    int p = pos;

    if (n > edlen - pos)
        n = edlen - pos;
    if (n <= 0)
        return;

    memmove(edbuf + p, edbuf + p + n, edlen - p - n + 1);
    edlen -= n;
    pos = p;
    int from = input_redraw_start(p);
    input_redraw_goto(from);

    if (line_status == 0)
        input_redraw_from(from);
}

/*
 * insert n bytes of UTF-8 at input line current position.
 * The position is set to after the inserted characters.
 */
void input_insert_follow_chars(char *str, int n)
{
    int p = pos;

    n = input_fit(str, n, BUFSIZE - edlen - 1);
    if (n <= 0)
        return;

    memmove(edbuf + p + n, edbuf + p, edlen + 1 - p);
    memmove(edbuf + p, str, n);
    edlen += n;
    pos = p + n;
    int from = input_redraw_start(p);
    input_redraw_goto(from);

    if (line_status == 0)
        input_redraw_from(from);
}

/*
 * replace 'len' bytes at byte offset 'from' of the input line with 'n'
 * bytes of UTF-8 from 'str'; the position is set to after them.  The
 * line is redrawn from the first character that differs, so that
 * completing a word does not draw its start again
 */
void input_replace(int from, int len, const char *str, int n)
{
    n = input_fit(str, n, BUFSIZE - 1 - (edlen - len));
    int same = 0;
    while (same < len && same < n && edbuf[from + same] == str[same])
        ++same;
    /* back to the start of the character that differs */
    while (same > 0 && same < n && utf8_is_continuation(str[same]))
        --same;
    memmove(edbuf + from + n, edbuf + from + len, edlen - from - len + 1);
    memcpy(edbuf + from, str, n);
    edlen += n - len;
    pos = from + n;
    int start = input_redraw_start(from + same);
    input_redraw_goto(start);

    if (line_status == 0)
        input_redraw_from(start);
}

void tty_puts(const char *s)
{
    while (*s != '\0')
        tty_putc(*s++);
}

/*
 * write character 'cp' in the locale's charset, which is not UTF-8: the
 * copyright sign as (c) and the dashes as -, whose bytes the charsets do
 * not agree on, except in the input line, which counts one cell for each
 */
static void tty_put_wc(uint32_t cp)
{
    if (!tty_write_state.input) {
        if (cp == U'©') {
            tty_put_wc('(');
            tty_put_wc('c');
            tty_put_wc(')');
            return;
        }
        /* U+2010 hyphen to U+2015 horizontal bar */
        if (cp >= U'‐' && cp <= U'―')
            cp = '-';
    }

    size_t r;
    bool ignore_error = false;
    /* a wchar_t of 16 bits, as on Cygwin, cannot hold it; no charset but
     * UTF-8, which does not come here, has it */
    wchar_t wc = cp <= WCHAR_MAX ? (wchar_t)cp : '?';
    if (tty_write_state.used + MB_LEN_MAX > sizeof(tty_write_state.data))
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
        ignore_error = true;
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
        if (tty_write_state.used >= sizeof(tty_write_state.data))
            tty_flush();
        tty_write_state.data[tty_write_state.used++] = b;
        return;
    }

    if (tty_write_state.npend > 0) {
        if (utf8_is_continuation(b)) {
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

    if (b <= ASCII_MAX) {
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
    char *old_locale = xstrdup(setlocale(LC_ALL, NULL));

    setlocale(LC_ALL, "C");

    char buf[1024];
    va_list va;
    va_start(va, format);
    int res = vsnprintf(buf, sizeof(buf), format, va);
    va_end(va);

    /* a longer text is formatted again into big */
    bool too_long = res >= 0 && (size_t)res >= sizeof(buf);
    char big[too_long ? res + 1 : 1];
    char *bufp = buf;
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

/* UTF-8 of a character read, not yet returned by tty_read(): up to two,
 * as a U+FFFD for an incomplete one before the next */
static char tty_out_buf[(CHARSET_DECODE_MAX > UTF8_MAX
                         ? CHARSET_DECODE_MAX
                         : UTF8_MAX)];
static size_t tty_out_start = 0, tty_out_used = 0;

/* has reading the terminal found the end of the input? */
static bool tty_eof = false;

bool tty_at_eof(void)
{
    return tty_eof;
}

bool tty_has_chars(void)
{
    return tty_in_used != tty_in_start || tty_out_used != tty_out_start;
}

/*
 * the character that the wchar_t 'wc' read is, or 0 if it is the first of
 * two: a wchar_t of 16 bits, as on Cygwin, holds a character above U+FFFF
 * as its UTF-16 surrogates, which 'high' keeps the first of
 */
static uint32_t wc_char(wchar_t wc, uint32_t *high)
{
    uint32_t cp = (uint32_t)wc;
#if WCHAR_MAX < UNICODE_MAX
    if (unicode_is_high_surrogate(cp)) {
        *high = cp;
        return 0;
    }
    if (unicode_is_low_surrogate(cp) && *high != 0)
        cp = utf16_join(*high, cp);
    *high = 0;
#endif
    return cp;
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
        /* invalid character; the state is undefined after it */
        ps = (mbstate_t){ 0 };
        if (*n == 0)
            return -1;
        --*n;
        memmove(s, s + 1, *n);
        goto again;
    }
    if (r == 0 && *n > 0 && s[0] == '\0') {
        *pwc = L'\0';
        r = 1;
    }
    return r;
}

/*
 * read up to 'count' bytes of UTF-8 from the terminal, converting
 * from the locale's charset; UTF-8 needs no conversion, only checking,
 * with invalid bytes as U+FFFD; return the number of bytes stored, or -1
 * if reading fails with an error other than EAGAIN
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

        if (tty_in_used > tty_in_start && tty_write_state.utf8) {
            static struct utf8_decoder dec;
            unsigned char b = tty_in_buf[tty_in_start++];
            tty_out_start = 0;
            tty_out_used = charset_decode_byte(CS_UTF8, &dec, b,
                                               tty_out_buf);
            continue;
        }
        if (tty_in_used > tty_in_start) {
            wchar_t wc;
            size_t avail = tty_in_used - tty_in_start;
            int converted = safe_mbtowc(&wc, tty_in_buf + tty_in_start,
                                        &avail);
            tty_in_used = tty_in_start + avail;
            if (converted >= 0) {
                static uint32_t high;
                if (converted == 0)
                    converted = 1;
                tty_in_start += converted;
                uint32_t cp = wc_char(wc, &high);
                if (cp == 0 && wc != L'\0')
                    continue;   /* the first half of a character */
                tty_out_start = 0;
                tty_out_used = utf8_put(cp, tty_out_buf);
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
                            sizeof(tty_in_buf) - tty_in_used);
        if (did_read < 0 && errno == EINTR)
            continue;
        if (did_read < 0 && errno != EAGAIN && errno != EWOULDBLOCK)
            return -1;
        if (did_read == 0)
            tty_eof = true;
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

    if (fgetws(ws, size, stdin) == NULL)
        return;

    uint32_t high = 0;
    for (const wchar_t *w = ws; *w != L'\0'; ++w) {
        uint32_t cp = wc_char(*w, &high);
        if (cp == 0)
            continue;           /* the first half of a character */
        char buf[UTF8_MAX];
        int n = utf8_put(cp, buf);
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
    if (len == 0)
        return;

    for (;;) {
        size_t s = sizeof(tty_write_state.data) - tty_write_state.used;
        if (s > len)
            s = len;
        memcpy(tty_write_state.data + tty_write_state.used, data, s);
        len -= s;
        tty_write_state.used += s;
        if (len == 0)
            break;
        data += s;
        tty_flush();
    }
}
