/*
 *  powwow  --  mud client with telnet protocol
 *
 *  Copyright (C) 1998,2000,2002 by Massimiliano Ghilardi
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *
 * History:
 *
 * Initially inspired to the Tintin client by Peter Unold,
 * Powwow contains no Tintin code.
 * The original program Cancan, written by Mattias Engdegård (Yorick)
 * (f91-men@nada.kth.se) 1992-94,
 * was greatly improved upon by Vivriel, Thuzzle and Ilie and then
 * transformed from Cancan into Powwow by Cosmos who worked
 * to make it yet more powerful.
 * AmigaDOS porting attempt by Fror.
 * Many new features added by Dain.
 * As usual, all the developers are in debt to countless users
 * for suggestions and debugging.
 * Maintenance was taken over by Steve Slaven in 2005.
 */

#include <assert.h>
#include <getopt.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <ctype.h>
#include <errno.h>
#include <time.h>

#include <sys/param.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <memory.h>
#include <unistd.h>

#include "defines.h"

#ifdef USE_LOCALE
#include <locale.h>
#endif

#include "beam.h"
#include "cmd.h"
#include "cmd2.h"
#include "edit.h"
#include "eval.h"
#include "gitversion.h"
#include "list.h"
#include "log.h"
#include "main.h"
#include "map.h"
#include "regexlib.h"
#include "tcp.h"
#include "tls.h"
#include "tty.h"
#include "utils.h"
#include "sgr.h"
#include "width.h"
#include "charname.h"

/* the start of the lines that continue the version and the authors */
#define VER_CONT "    "

#define HELPNAME "powwow.help"
#define COPYNAME "COPYING"
#define NAMESNAME "unicode-names.txt"
#define EMOJINAME "emoji-names.txt"

/*     local function declarations       */
#ifdef MOTDFILE
static void printmotd(void);
#endif
static noreturn void mainloop(void);
static void exec_delays(void);
static void get_remote_input(fd_set *read_fds, unsigned *ncache_read_fds);
static void get_user_input(void);

static bool search_action_or_prompt(char *line, bool clearline, int onprompt);
#define search_action(line, clearline) \
    search_action_or_prompt((line), (clearline), 0)
#define search_prompt(line, copyprompt) \
    search_action_or_prompt((line), false, (copyprompt) + 1)

static void set_params(char *line, int *match_s, int *match_e);
static void parse_commands(char *command, char *arg);
static bool subst_param(ptr **buf, char *src);
static bool jit_subst_vars(ptr **buf, char *src);

/*        GLOBALS                */
static const char *helpname = HELPNAME;
static const char *copyname = COPYNAME;

long received = 0;      /* amount of data received from remote host */
long sent = 0;          /* amount of data sent to remote host */

volatile bool confirm = false;  /* true if just tried to quit */
int history_done = 0;   /* number of recursive #history commands */
int prompt_status = 0;  /* prompt status: 0 = ready -> nothing to do;
                         *  1 if internal echo -> must redraw;
                         * -1 if something sent to MUD -> waiting for it.
                         */
int line_status = 0;    /* input line status: 0 = ready -> nothing to do;
                         *  1 if printed something -> must redraw.
                         */

int limit_mem = 0;      /* if != 0, max len of a string or text */

bool opt_echo = true;           /* true if text sent to MUD must be echoed */
bool opt_keyecho = true;        /* true if binds must be echoed */
bool opt_info = true;           /* false if internal messages are suppressed */
bool opt_exit = false;          /* true to autoquit when closing last conn. */
bool opt_history = false;       /* true if to save also history */
bool opt_words = false;         /* true if to save also word completion list */
bool opt_compact = false;       /* true if to clear prompt between remote
                                 * messages */
bool opt_debug = false;         /* true if to echo every line before executing
                                 * it */
bool opt_speedwalk = false;     /* true: speedwalk on */
bool opt_wrap = false;          /* true: word wrap active */
bool opt_autoprint = false;     /* true: automatically #print lines matched by
                                 * actions */
bool opt_reprint = false;       /* true: reprint sent commands when we get a
                                 * prompt */
bool opt_sendsize = false;      /* true: send term size upon connect */
bool opt_autoclear = true;      /* true: clear input line before executing
                                 * commands from spawned programs.
                                 * if false, spawned progs must #clear before
                                 * printing
                                 */

struct hostinfo hostinfo;
bool default_utf8 = true;
#ifdef USE_TLS
bool default_tls = true;
#else
bool default_tls = false;
#endif

static char powwow_dir[BUFSIZE];   /* default path to definition files */
char deffile[BUFSIZE];             /* name and path of definition file */
char helpfile[BUFSIZE];            /* name and path of help file */
char copyfile[BUFSIZE];            /* name and path of copyright file */
aliasnode *aliases[MAX_HASH];      /* head of alias hash list */
aliasnode *sortedaliases;          /* head of (ASCII) sorted alias list */
actionnode *actions;               /* head of action list */
promptnode *prompts;               /* head of prompt list */
marknode *markers;                 /* head of mark list */
substnode *substitutions;          /* head of substitution list */
int a_nice = 0;                    /* default priority of new
                                    * actions/marks/substitutions */
keynode *keydefs;                  /* head of key binding list */
delaynode *delays;                 /* head of delayed commands list */
delaynode *dead_delays;            /* head of dead-delayed commands list */

struct named_vars named_vars[VAR_KINDS];

/* the numbered variables */
static struct {
    unnamedvar globals[GLOBAL_VAR_COUNT];       /* @-50..@-1 */
    unnamedvar params[MAX_STACK][PARAM_COUNT];  /* @0..@9 per call */
    int depth;                                  /* current call */
} vars;

/* the variable @n or $n, for n from -GLOBAL_VAR_COUNT to PARAM_COUNT - 1 */
static unnamedvar *numbered_var(int n)
{
    assert(-GLOBAL_VAR_COUNT <= n && n < PARAM_COUNT);
    return (n < 0
            ? &vars.globals[n + GLOBAL_VAR_COUNT]
            : &vars.params[vars.depth][n]);
}

long *num_var(int n)
{
    return &numbered_var(n)->num;
}

ptr **text_var(int n)
{
    return &numbered_var(n)->str;
}

varnode *prompt;                   /* $prompt is always set */
ptr *marked_prompt;                /* $prompt with marks added */
static varnode *last_line;         /* $line is always set to
                                    * the last line processed */

vtime now;                         /* current time */
bool now_updated;                  /* current time is up to date */
vtime start_time;                  /* time of powwow timer startup */
vtime ref_time;                    /* time corresponding to timer == 0 */

function_str last_edit_cmd;        /* to detect repeated commands */

clock_t start_clock, cpu_clock;

char initstr[BUFSIZE];             /* initial string to send on connect */

int linemode = 0;                  /* line mode flags (LM_* in main.h) */

/* for line editing */
int cols = 80, lines = 24; /* screen size */
int cols_1 = 79;          /* == cols if tty_wrapglitch, == cols-1 otherwise */
int olines;               /* previous screen size */
int col0;                 /* input line offset (= printstrlen of prompt) */
int line0;                /* screen line where the input line starts */
char edbuf[BUFSIZE];      /* line editing buffer */
int edlen;                /* length of current input line */
int pos = 0;              /* cursor position in line */
int edstart = 0;          /* where the text that can be edited starts, after
                           * a prompt in edbuf such as for C-x 8 Ret */
int edlabel = 0;          /* the bytes at the start of edbuf that are a
                           * label of powwow, drawn as its messages */
int isprompt_len = 0;     /* set by #isprompt: 0 if not a prompt, -1 if a
                           * prompt of unknown length, else its length */
bool verbatim = false;    /* don't expand aliases or process semicolons */
char prefixstr[BUFSIZE];  /* inserted in the editing buffer each time */
char inserted_next[BUFSIZE]; /* inserted in buffer just once */
bool flashback = false;   /* cursor is on excursion and should be put back */
int excursion;            /* where the excursion is */
char edattrbeg[ATTR_SGR_MAX];   /* starting input line attributes */
char edattrend[ATTR_SGR_MAX];   /* ending input line attributes */
bool edattrbg;            /* input line attributes do change bg color */

/* signals handling */
volatile int sig_pending, sig_winch_got, sig_chld_got;

/* different ID characters for different action types */
/*
 * Cosmos: they are hardcoded in cmd2.c, function parse_action()
 * so don't change them.
 */
const char action_chars[ACTION_TYPES] = { '>', '%' };

/* update doc/powwow.md.in and doc/powwow.help if changed */
#define DELIMSET_NORMAL   " ;"
#define DELIMSET_PROGRAM  " <>!=(),;\"'{}[]+-/*%"

const char *delim_list[] = { DELIMSET_NORMAL, DELIMSET_PROGRAM, NULL };
int delim_len[] = {
    sizeof DELIMSET_NORMAL - 1, sizeof DELIMSET_PROGRAM - 1, 0
};
const char *const delim_name[] = { "normal", "program", "custom" };
int delim_mode = DELIM_NORMAL;

/* Group delimiter */
char *group_delim;

static void show_help(const char *prog)
{
#ifdef HAVE_GETOPT_LONG
#define HELP_ARG "--help, -h         "
#define TLS_ARG  "--tls[=TLS-OPTIONS]"
#define NTLS_ARG "--no-tls, -n       "
#define UTF8_ARG "--utf8, -u         "
#define LAT1_ARG "--latin1, -l       "
#define VER_ARG  "--version, -v      "
#define VER_DESC "display the powwow version (twice: with details)"
#else
#define HELP_ARG "-h                "
#define TLS_ARG  "-t, -T TLS-OPTIONS"
#define NTLS_ARG "-n                "
#define UTF8_ARG "-u                "
#define LAT1_ARG "-l                "
#define VER_ARG  "-v                "
#define VER_DESC "display the powwow version (-vv: with details)"
#endif

#ifdef USE_TLS
#define TLS_DESC " (default); available TLS-OPTIONS:\n"         \
    "        no-verify     do not verify CA certificate\n"      \
    "        host=HOST     assume the CA hostname is HOST"
#else
#define TLS_DESC " (not supported)"
#endif

    printf("Usage:\n"
           "  %s [OPTIONS] [DEFINITION-FILE] [HOST PORT]\n"
           "\n"
           "Options:\n"
           "  " HELP_ARG "    display this help text\n"
           "  " TLS_ARG  "    enable TLS" TLS_DESC "\n"
           "  " NTLS_ARG "    disable TLS\n"
           "  " UTF8_ARG "    assume and prefer UTF-8 on the connection"
           " (default)\n"
           "  " LAT1_ARG "    assume and prefer ISO-8859-1 on the connection\n"
           "  " VER_ARG  "    " VER_DESC "\n",
           prog);
#undef HELP_ARG
#undef TLS_ARG
#undef NTLS_ARG
#undef UTF8_ARG
#undef LAT1_ARG
#undef VER_ARG
#undef VER_DESC
#undef TLS_DESC
}

/*
 * set 'path', of BUFSIZE, to the file 'name' of the names of characters
 * or emoji, next to the help file; if it is not there, to the same name
 * ending in .gz, compressed by gzip, as installed if powwow has zlib
 */
static void names_file_path(char *path, const char *name)
{
    strcpy(path, helpfile);
    char *slash = strrchr(path, '/');
    strcpy(slash != NULL ? slash + 1 : path, name);
#ifdef HAVE_ZLIB
    size_t len = strlen(path);
    if (access(path, R_OK) != 0 && len + sizeof(".gz") <= BUFSIZE)
        strcpy(path + len, ".gz");
#endif
}

/*
 * does powwow run from its build directory, as the 'argv0' it was run as
 * says? then it uses the documentation in its source tree
 */
static bool in_build_dir(const char *argv0)
{
    const char *slash = strrchr(argv0, '/');
    if (slash == NULL)
        return false;           /* found in $PATH */
    char dir[BUFSIZE];
    snprintf(dir, sizeof(dir), "%.*s",
             slash == argv0 ? 1 : (int)(slash - argv0), argv0);
    char *real = realpath(dir, NULL);
    char *build = realpath(POWWOW_BUILD_DIR, NULL);
    bool same = real != NULL && build != NULL && strcmp(real, build) == 0;
    free(real);
    free(build);
    return same;
}

int main(int argc, char **argv)
{
#ifdef USE_LOCALE
    if (setlocale(LC_ALL, "") == NULL)
        fprintf(stderr, "Failed setlocale(LC_ALL, \"\")\n");
#endif

#ifdef HAVE_GETOPT_LONG
    static const struct option longopts[] = {
        { "help",    no_argument,       NULL, 'h' },
        { "tls",     optional_argument, NULL, 't' },
        { "no-tls",  no_argument,       NULL, 'n' },
        { "latin1",  no_argument,       NULL, 'l' },
        { "utf8",    no_argument,       NULL, 'u' },
        { "version", no_argument,       NULL, 'v' },
        { NULL }
    };
#endif
    int cmdline_utf8 = -1;      /* --utf8: 1, --latin1: 0 */
    struct tls_options cmdline_tlsopt = { .use_tls = false };
    bool cmdline_tls = false;   /* --tls or --no-tls given */
    int nversion = 0;           /* number of --version */
    for (int c;;) {
#ifdef HAVE_GETOPT_LONG
        c = getopt_long(argc, argv, "hlnt::uv", longopts, NULL);
#else
        c = getopt(argc, argv, "hlntT:uv");
#endif
        if (c == -1)
            break;
        switch (c) {
        case '?':
            exit(EXIT_FAILURE);
        case 'h':
            show_help(argv[0]);
            exit(EXIT_SUCCESS);
        case 't':
#ifndef HAVE_GETOPT_LONG
            optarg = NULL;      /* only -T accepts arguments */
#endif
        case 'T': {
            const char *arg = optarg != NULL ? optarg : "";
            if (!parse_tls_flag(&cmdline_tlsopt, &arg, false)) {
                fprintf(stderr, "invalid TLS options: '%s'\n", optarg);
                exit(EXIT_FAILURE);
            }
            cmdline_tls = true;
            break;
        }
        case 'n':
            free(cmdline_tlsopt.hostname);
            cmdline_tlsopt = (struct tls_options){ .use_tls = false };
            cmdline_tls = true;
            break;
        case 'l':
            cmdline_utf8 = 0;
            break;
        case 'u':
            cmdline_utf8 = 1;
            break;
        case 'v':
            ++nversion;
            break;
        default:
            abort();
        }
    }

    if (nversion > 0) {
        printver(nversion > 1 ? 2 : 0);
        tty_flush();
        exit(EXIT_SUCCESS);
    }

    default_utf8 = cmdline_utf8 != 0;
    hostinfo.utf8 = default_utf8;
    if (cmdline_tls)
        default_tls = cmdline_tlsopt.use_tls;
    hostinfo.tlsopt = cmdline_tlsopt;
    hostinfo.tlsopt.use_tls = default_tls;

    int nargs = argc - optind;
    if (nargs > 3) {
        fprintf(stderr, "%s: too many arguments\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    /* initializations */
    group_delim = my_strdup("@");

    update_now();
    ref_time = now;
    start_time = now;
    movie_last = now;

    start_clock = clock();
    cpu_clock = start_clock;
    srand48(now.tv_sec ^ now.tv_usec ^ ((long)getpid() << 16));

    initialize_cmd();

    const char *dir = getenv("POWWOWDIR");
    if (dir != NULL) {
        strcpy(powwow_dir, dir);
        if (powwow_dir[strlen(powwow_dir) - 1] != '/')
            strcat(powwow_dir, "/");
    } else {
        powwow_dir[0] = '\0';
    }

    bool from_build_dir = in_build_dir(argv[0]);
    const char *help = getenv("POWWOWHELP");
    if (help != NULL)
        strcpy(helpfile, help);
    else if (powwow_dir[0] != '\0')
        strcpy(helpfile, powwow_dir);
    else if (from_build_dir)
        strcpy(helpfile, POWWOW_SOURCE_DIR "/doc");
    else
        strcpy(helpfile, POWWOW_DIR);

    if (helpfile[strlen(helpfile) - 1] != '/')
        strcat(helpfile, "/");
    strcat(helpfile, helpname);
    if (access(helpfile, R_OK) == -1 && access(helpname, R_OK) == 0)
        strcpy(helpfile, helpname);

    names_file_path(namesfile, NAMESNAME);
    names_file_path(emojifile, EMOJINAME);

    if (powwow_dir[0] != '\0')
        strcpy(copyfile, powwow_dir);
    else if (from_build_dir)
        strcpy(copyfile, POWWOW_SOURCE_DIR);
    else
        strcpy(copyfile, POWWOW_DIR);
    if (copyfile[strlen(copyfile) - 1] != '/')
        strcat(copyfile, "/");
    strcat(copyfile, copyname);
    if (access(copyfile, R_OK) == -1 && access(copyname, R_OK) == 0)
        strcpy(copyfile, copyname);

    /* allocate permanent variables */
    if ((prompt = add_varnode("prompt", VAR_TEXT)) == NULL
        || (prompt->str = ptrnew(PARAMLEN)) == NULL
        || (marked_prompt = ptrnew(PARAMLEN)) == NULL
        || (last_line = add_varnode("last_line", VAR_TEXT)) == NULL
        || (last_line->str = ptrnew(PARAMLEN)) == NULL
        || MEM_ERROR)
        syserr("out of memory");

    edit_bootstrap();
    tty_bootstrap();

#ifdef MOTDFILE
    printmotd();
#endif

    printver(1);

    if (nargs == 0) {
        message_wrap(VER_CONT, "\nPowwow comes with ABSOLUTELY NO WARRANTY; "
                     "for details type \"#help warranty\".\nThis is free "
                     "software, and you are welcome to redistribute it "
                     "under certain conditions; type \"#help copyright\" "
                     "for details.");
    }

    bool read_file = false;     /* run initstr ? */
    if (nargs == 0 || nargs == 2) {
        tty_add_initial_binds();
        tty_add_walk_binds();
    } else if (nargs == 1 || nargs == 3) {
        /*
         * assuming first arg is definition file name
         * If three args, first is definition file name,
         * second and third are hostname and port number
         * (they overwrite the ones in definition file)
         */
        set_deffile(argv[optind++]);

        if (access(deffile, R_OK) == -1 || access(deffile, W_OK) == -1) {
            tty_printf("Creating %s\nHost name  :", deffile);
            tty_flush();
            tty_gets(hostinfo.name, BUFSIZE);
            if (hostinfo.name[0] == '\n')
                hostinfo.name[0] = '\0';
            else
                strtok(hostinfo.name, "\n");
            tty_puts("Port number:");
            tty_flush();
            char portnum[INTLEN];
            tty_gets(portnum, INTLEN);
            hostinfo.port = atoi(portnum);
            tty_add_initial_binds();
            tty_add_walk_binds();
            limit_mem = 1048576;
            if (save_settings() < 0)
                exit(0);
        } else if (read_settings() < 0) {
            exit(0);
        } else {
            read_file = true;
        }
    }

    /* the definition file's #host must not override --utf8/--latin1
     * or --tls/--no-tls */
    if (cmdline_utf8 >= 0)
        hostinfo.utf8 = cmdline_utf8 != 0;
    if (cmdline_tls)
        hostinfo.tlsopt = cmdline_tlsopt;

    nargs = argc - optind;
    if (nargs == 2) {
        /* assume last two args are hostname and port number */
        my_strncpy(hostinfo.name, argv[optind], BUFSIZE - 1);
        hostinfo.port = atoi(argv[optind + 1]);
    }

    signal_start();
    tty_start();

    tty_puts(tty_clreoscr);
    tty_putc('\n');
    col0 = 0;
    tty_gotoxy(col0, lines - 2);
    tty_puts("Type #help for help.\n");
    line0 = lines - 1;

    FD_ZERO(&fdset);
    FD_SET(tty_read_fd, &fdset);

    if (*hostinfo.name != '\0') {
        tcp_open("main", (*initstr != '\0' ? initstr : NULL), hostinfo.name,
                 hostinfo.port, &hostinfo.tlsopt, hostinfo.utf8);
    }

    if (read_file && *hostinfo.name == '\0' && *initstr != '\0') {
        parse_instruction(initstr, false, false, true);
        history_done = 0;
    }

    confirm = false;

    mainloop();

    /* NOTREACHED */
    return 0;
}

/*
 * show current version
 */
void printver(int level)
{
    /* a -dev version with the commit it was built from, if from git */
    static const char dev[] = "-dev";
    const char *version = VERSION;
    size_t len = strlen(version);
    bool commit = (GIT_COMMIT[0] != '\0' && len >= sizeof dev - 1
                   && strcmp(version + len - (sizeof dev - 1), dev) == 0);

    /* the authors are in README; a copyright sign and an en dash if the
     * terminal can show them */
    bool utf8 = tty_locale_utf8();
    message_wrap(VER_CONT, "Powwow version %s%s%s%s, %s 1998%s2026 by the "
                 "powwow authors", VERSION, commit ? " (" : "",
                 commit ? GIT_COMMIT : "", commit ? ")" : "",
                 utf8 ? "\302\251" : "Copyright",
                 utf8 ? "\342\200\223" : "-");
    if (level < 1)
        return;
    message_wrap(VER_CONT, "https://github.com/MUME/powwow");
    if (level < 2)
        return;

    /* as in README, spelled in ASCII if the terminal cannot show them */
    message_wrap(VER_CONT, "%s",
                 (utf8
                  ? "\nAuthors: Massimiliano Ghilardi (Cosmos), "
                    "Steve Slaven (bpk), Fr\303\263r, D\303\241in and "
                    "others\nBased on Cancan by Mattias Engdeg\303\245rd "
                    "(Yorick), Vivriel, Thuzzle and Ilie"
                  : "\nAuthors: Massimiliano Ghilardi (Cosmos), "
                    "Steve Slaven (bpk), Fror, Dain and others\n"
                    "Based on Cancan by Mattias Engdegard (Yorick), "
                    "Vivriel, Thuzzle and Ilie"));

    message_wrap(VER_CONT, "\nOptions: "
#ifdef USE_VT100
                 "vt100-only"
#else
                 "termcaps"
#endif
#ifdef USE_REGEXP
  #ifdef USE_REGEXP_PCRE2POSIX
                 ", regexp (pcre2posix)"
  #else
                 ", regexp (libc)"
  #endif
#else
                 ", no regexp"
#endif
#ifdef USE_LOCALE
                 ", locale"
#endif
#ifdef USE_TLS
                 ", tls"
#endif
#ifdef HAVE_LIBDL
                 ", modules"
#endif
#ifdef HAVE_ZLIB
                 ", zlib"
#endif
                 );
}

#ifdef MOTDFILE
/*
 * print the message of the day if present
 */
static void printmotd(void)
{
    FILE *f = fopen(MOTDFILE, "r");
    if (f != NULL) {
        char line[BUFSIZE];
        while (utf8_fgets(line, BUFSIZE, f) != NULL)
            tty_puts(line);
        fclose(f);
    }
}
#endif

static void redraw_everything(void)
{
    if (prompt_status == 1 && line_status == 0)
        line_status = 1;
    if (prompt_status == 1) {
        draw_prompt();
    } else if (prompt_status == -1) {
        promptzero();
        isprompt_len = 0;
        col0 = 0;
    }
    if (line_status == 1)
        draw_input_line();
}

static char typed[CAPLEN];     /* keys typed so far, that start a binding */
static int nchars;              /* number of them */
static vtime typed_time;        /* when the last of them was typed */
static bool typed_shown;        /* whether they are shown after the line */

/* the time to wait for the next key before showing the keys typed so far */
enum { TYPED_HINT_DELAY = 1000 };

/* the milliseconds left before showing the keys typed so far, or -1 */
static int typed_hint_wait(void)
{
    if (nchars == 0 || typed_shown || (linemode & (LM_CHAR | LM_NOECHO)))
        return -1;
    update_now();
    long waited = diff_vtime(&now, &typed_time);
    return waited >= TYPED_HINT_DELAY ? 0 : TYPED_HINT_DELAY - (int)waited;
}

/*
 * write the names of the 'len' keys of 'seq', as "C-x 8", into 'buf' of
 * 'size' bytes, leaving room for a few more
 */
static void key_names(const char *seq, int len, char *buf, size_t size)
{
    size_t n = 0;
    *buf = '\0';
    for (int i = 0; i < len && n + 8 < size; ++i) {
        unsigned char c = seq[i];
        const char *sep = i > 0 ? " " : "";
        if (c == ASCII_ESC)
            n += snprintf(buf + n, size - n, "%sESC", sep);
        else if (c < ' ')
            n += snprintf(buf + n, size - n, "%sC-%c", sep, c + '`');
        else
            n += snprintf(buf + n, size - n, "%s%c", sep, c);
    }
}

/* show the keys typed so far after the input line, as "C-x 8-" */
static void show_typed_hint(void)
{
    char hint[64];
    key_names(typed, nchars, hint, sizeof(hint) - 1);
    strcat(hint, "-");
    input_show_hint(hint);
    typed_shown = true;
}

/* how much can we sleep in select() ? */
static void compute_sleeptime(vtime **timeout, bool force_zero)
{
    static vtime tbuf;
    if (force_zero) {
        tbuf = (vtime){ 0 };
        *timeout = &tbuf;
        return;
    }

    int sleeptime = 0;

    if (delays != NULL) {
        update_now();
        sleeptime = diff_vtime(&delays->when, &now);
        if (sleeptime == 0)
            sleeptime = 1;    /* if sleeptime is less than 1 millisec,
                               * set to 1 millisec */
    }
    if (flashback && (sleeptime == 0 || sleeptime > FLASHDELAY))
        sleeptime = FLASHDELAY;
    int wait = typed_hint_wait();
    if (wait >= 0 && (sleeptime == 0 || sleeptime > wait))
        sleeptime = wait > 0 ? wait : 1;

    if (sleeptime != 0) {
        tbuf.tv_sec = sleeptime / mSEC_PER_SEC;
        tbuf.tv_usec = (sleeptime % mSEC_PER_SEC) * uSEC_PER_mSEC;
        *timeout = &tbuf;
    } else {
        *timeout = NULL;
    }
}

/*
 * main loop.
 */
static noreturn void mainloop(void)
{
    fd_set cache_read_fds;
    unsigned ncache_read_fds = 0;

    FD_ZERO(&cache_read_fds);

    for (;;) {
        current_connection = main_connection;
        exec_delays();

        fd_set readfds;
        int err;
        do {
            if (sig_pending != 0)
                sig_bottomhalf(); /* this might set errno... */

            tcp_flush();

            if (!(pos <= edlen)) {
                message_printf("\n#*ARGH* assertion failed (pos <= edlen)\n");
                pos = edlen;
            }

            redraw_everything();
            tty_flush();

            vtime *timeout;
            compute_sleeptime(&timeout,
                              ncache_read_fds > 0 || tty_has_chars());

            error = 0;
            now_updated = false;

            readfds = fdset;
            int max_fd = edit_output_fds(&readfds, tcp_max_fd);
            err = select(max_fd + 1, &readfds, NULL, NULL, timeout);
        } while (err < 0 && errno == EINTR);

        if (err < 0)
            syserr("cannot wait for input");
        if (typed_hint_wait() == 0)
            show_typed_hint();
        if (tty_has_chars() && !FD_ISSET(tty_read_fd, &readfds)) {
            /* keys already read from the terminal */
            FD_SET(tty_read_fd, &readfds);
            ++err;
        }
        if (ncache_read_fds > 0) {
            for (int fd = 0; ncache_read_fds > 0; ++fd) {
                if (FD_ISSET(fd, &cache_read_fds)) {
                    FD_CLR(fd, &cache_read_fds);
                    --ncache_read_fds;
                    if (!FD_ISSET(fd, &readfds)) {
                        FD_SET(fd, &readfds);
                        ++err;
                    }
                }
            }
        }

        if (flashback)
            putbackcursor();

        if (err > 0)
            err -= edit_read_output(&readfds);

        /* process subsidiary and spawned connections first */
        if (tcp_count > 1 || tcp_attachcount != 0) {
            for (int i = 0; err != 0 && i < conn_max_index; ++i) {
                if (CONN_INDEX(i).id != NULL
                    && &CONN_INDEX(i) != main_connection) {
                    current_connection = &CONN_INDEX(i);
                    if (FD_ISSET(current_connection->fd, &readfds)) {
                        --err;
                        get_remote_input(&cache_read_fds, &ncache_read_fds);
                    }
                }
            }
        }
        /* and main connection last */
        if (main_connection != NULL
            && FD_ISSET(main_connection->fd, &readfds)) {
            current_connection = main_connection;
            get_remote_input(&cache_read_fds, &ncache_read_fds);
        }
        if (FD_ISSET(tty_read_fd, &readfds)) {
            current_connection = main_connection;
            confirm = false;
            get_user_input();
        }
    }
}

/*
 * set the new prompt / input line status
 */
void status(int s)
{
    if (s < 0) {
        /* waiting prompt from the MUD */
        prompt_status = s;
        line_status = 1;
    } else {
        if (prompt_status >= 0)
            prompt_status = s;
        if (line_status >= 0)
            line_status = s;
    }
}

/*
 * execute the delayed labels that have expired
 * and place them in the disabled delays list
 */
static void exec_delays(void)
{
    if (delays == NULL)
        return;

    update_now();

    if (cmp_vtime(&delays->when, &now) > 0)
        return;

    /* remember delayed command may modify the prompt and/or input line! */
    if (prompt_status == 0) {
        clear_input_line(opt_compact || !opt_info);
        if (!opt_compact && opt_info && prompt_status == 0
            && promptlen != 0) {
            tty_putc('\n');
            col0 = 0;
            status(1);
        }
    }

    ptr *buf = NULL;
    while (delays != NULL && cmp_vtime(&delays->when, &now) <= 0) {
        /* remove delayed command from active list and put it in the dead
         * one */
        delaynode *dying = delays;
        delays = dying->next;

        insert_delaynode(dying, true);

        /* must be moved before executing delay->command
         * and command must be copied in a buffer
         * (can't you imagine why? The command may edit itself...)
         */

        if (opt_info)
            tty_message("#now [%s]\n", dying->command);

        if (*dying->command != '\0') {
            error = 0;

            buf = ptrmcpy(buf, dying->command, strlen(dying->command));
            if (MEM_ERROR) {
                print_error(error);
            } else {
                parse_instruction(ptrdata(buf), false, false, true);
                history_done = 0;
            }
        }
    }
    ptrdel(&buf);
}

/* append 'len' bytes to the buffer at *buf of *size bytes, *used used */
static void buf_add(char **buf, size_t *used, size_t *size,
                    const char *s, size_t len)
{
    if (*used + len + 1 > *size) {
        *size = 2 * (*used + len + 1);
        *buf = xrealloc(*buf, *size);
    }
    assert(*buf != NULL);
    memcpy(*buf + *used, s, len);
    *used += len;
    (*buf)[*used] = '\0';
}

/*
 * append the SGR sequences that change the rendition from 'from' to
 * 'to'; both are relative to 'base', the rendition where the prompt
 * starts
 */
static void sgr_transition(char **buf, size_t *used, size_t *size,
                           const struct sgr_state *from,
                           const struct sgr_state *to, const char *base)
{
    static const char reset[] = "\033[0m";

    if (to->reset == from->reset && to->len >= from->len
        && memcmp(to->seq, from->seq, from->len) == 0) {
        /* just more sequences */
        buf_add(buf, used, size, to->seq + from->len, to->len - from->len);
        return;
    }
    buf_add(buf, used, size, reset, sizeof reset - 1);
    if (!to->reset)
        buf_add(buf, used, size, base, strlen(base));
    buf_add(buf, used, size, to->seq, to->len);
}

/* one cell per terminal column, for effective_prompt() */
enum { NARROW, WIDE_LEFT, WIDE_RIGHT };
struct cell {
    char  *text;                /* UTF-8: a grapheme cluster */
    size_t len, size;           /* bytes used and allocated in text */
    struct cell_state cs;       /* the cluster, to join more text to it */
    unsigned char kind;         /* NARROW, WIDE_LEFT or WIDE_RIGHT */
    int    attr;                /* index into attrs */
    char  *other;               /* other escapes and controls before it */
};

/* make the 'n' bytes at 's', one character, the text of 'cl' */
static void cell_set(struct cell *cl, const char *s, size_t n)
{
    uint32_t cp;
    utf8_get(s, n, &cp);
    cell_start(&cl->cs, cp);
    cl->len = 0;
    buf_add(&cl->text, &cl->len, &cl->size, s, n);
}

/*
 * compute the effective prompt string, which may end in \b* or \r:
 * emulate the terminal for \b, \r and ESC [ K, where escape sequences
 * take no room on screen, wide characters take two columns, and
 * characters keep the rendition (colors etc.) they were written with
 */
static void effective_prompt(void)
{
    const char *const pstr = promptstr;
    const size_t len = promptlen;

    size_t k;
    for (k = 0; k < len; ++k) {
        if (pstr[k] == '\b' || pstr[k] == '\r'
            || (pstr[k] == ASCII_ESC && len - k >= 3
                && pstr[k + 1] == '[' && pstr[k + 2] == 'K'))
            break;
    }
    if (k == len)
        return;                 /* nothing to emulate */

    const size_t maxcells = 2 * len + 2;
    struct cell *cells = xcalloc(maxcells, sizeof *cells);
    struct sgr_state *attrs = xmalloc((len + 1) * sizeof *attrs);
    char *pend = NULL;          /* other escapes and controls, pending */
    size_t npend = 0, pendsize = 0;
    size_t at = 0, ncells = 0, nattrs = 0;
    struct sgr_state cur;
    int cur_attr = -1;          /* index of cur in attrs, if any */

    sgr_reset(&cur);

/* overwriting half of a wide character blanks the other half */
#define UNSPLIT(i) do {                                                 \
        if ((i) < ncells && cells[i].kind == WIDE_RIGHT && (i) > 0) {   \
            cells[(i) - 1].kind = NARROW;                               \
            cell_set(&cells[(i) - 1], " ", 1);                          \
        } else if ((i) < ncells && cells[i].kind == WIDE_LEFT           \
                   && (i) + 1 < ncells) {                               \
            cells[(i) + 1].kind = NARROW;                               \
            cell_set(&cells[(i) + 1], " ", 1);                          \
        }                                                               \
    } while (0)

    for (size_t p = 0; p < len; ++p) {
        char c = pstr[p];
        if (c == '\b') {
            if (at > 0)
                --at;
        } else if (c == '\r') {
            at = 0;
        } else if (c == ASCII_ESC) {
            size_t q = p + 1;
            if (q < len && pstr[q] == '[') {
                /* CSI: parameter bytes, then a final byte */
                for (++q; q < len && is_csi_param(pstr[q]); ++q)
                    ;
                if (q < len
                    && (pstr[q] == 'K'
                        && (q == p + 2
                            || (q == p + 3 && pstr[p + 2] == '0')))) {
                    /* erase to end of line */
                    UNSPLIT(at);
                    for (size_t i = at; i < ncells; ++i)
                        free(cells[i].other);
                    if (ncells > at)
                        ncells = at;
                    p = q;
                    continue;
                }
                if (q < len && pstr[q] == 'm') {
                    sgr_apply(&cur, pstr + p, q + 1 - p);
                    cur_attr = -1;
                    p = q;
                    continue;
                }
            }
            /* some other (possibly incomplete) escape sequence */
            if (q < len)
                ++q;
            buf_add(&pend, &npend, &pendsize, pstr + p, q - p);
            p = q - 1;
        } else if ((unsigned char)c < ' ' || c == ASCII_DEL) {
            buf_add(&pend, &npend, &pendsize, &c, 1);
        } else {
            uint32_t cp;
            int n = utf8_get(pstr + p, len - p, &cp);
            int w = cp_width(cp);
            size_t i = at - 1;
            if (at > 0 && cells[i].kind == WIDE_RIGHT && i > 0)
                --i;
            if (at > 0 && cell_add(&cells[i].cs, cp)) {
                /* e.g. a combining character: joins the previous cell */
                buf_add(&cells[i].text, &cells[i].len, &cells[i].size,
                        pstr + p, n);
                if (cells[i].kind == NARROW && i + 1 == at
                    && cells[i].cs.width == 2) {
                    /* e.g. VS16 made it an emoji, two columns wide */
                    UNSPLIT(at);
                    if (at >= ncells) {
                        cells[at].other = NULL;
                        ncells = at + 1;
                    }
                    cells[i].kind = WIDE_LEFT;
                    cells[at].len = 0;
                    cells[at].kind = WIDE_RIGHT;
                    cells[at].attr = cells[i].attr;
                    ++at;
                }
                p += n - 1;
                continue;
            }
            if (w == 0) {
                /* e.g. a C1 control character */
                buf_add(&pend, &npend, &pendsize, pstr + p, n);
                p += n - 1;
                continue;
            }
            if (cur_attr < 0) {
                attrs[nattrs] = cur;
                cur_attr = nattrs;
                ++nattrs;
            }
            UNSPLIT(at);
            if (w == 2)
                UNSPLIT(at + 1);
            for (int half = 0; half < w; ++half) {
                struct cell *cl = &cells[at];
                if (at >= ncells) {
                    cl->other = NULL;
                    ncells = at + 1;
                }
                if (half == 0 && npend > 0) {
                    size_t used = cl->other != NULL ? strlen(cl->other) : 0;
                    size_t size = used != 0 ? used + 1 : 0;
                    buf_add(&cl->other, &used, &size, pend, npend);
                    npend = 0;
                }
                if (half == 0) {
                    cell_set(cl, pstr + p, n);
                    cl->kind = w == 2 ? WIDE_LEFT : NARROW;
                } else {
                    cl->len = 0;
                    cl->kind = WIDE_RIGHT;
                }
                cl->attr = cur_attr;
                ++at;
            }
            p += n - 1;
        }
    }
#undef UNSPLIT

    /* build the result */
    const char *base = sgr_prompt_base();
    char *out = NULL;
    size_t used = 0, size = 0;
    struct sgr_state emitted;
    sgr_reset(&emitted);
    for (size_t i = 0; i < ncells; ++i) {
        if (cells[i].other != NULL) {
            buf_add(&out, &used, &size, cells[i].other,
                    strlen(cells[i].other));
            free(cells[i].other);
        }
        if (cells[i].kind == WIDE_RIGHT)
            continue;           /* drawn with its left half */
        sgr_transition(&out, &used, &size, &emitted, &attrs[cells[i].attr],
                       base);
        emitted = attrs[cells[i].attr];
        if (cells[i].len > 0)
            buf_add(&out, &used, &size, cells[i].text, cells[i].len);
    }
    sgr_transition(&out, &used, &size, &emitted, &cur, base);
    if (npend > 0)
        buf_add(&out, &used, &size, pend, npend);
    if (at < ncells) {
        /* the cursor is not at the end: move it back */
        if (at == 0) {
            buf_add(&out, &used, &size, "\r", 1);
        } else {
            for (size_t i = at; i < ncells; ++i)
                buf_add(&out, &used, &size, "\b", 1);
        }
    }

    prompt->str = ptrmcpy(prompt->str, out != NULL ? out : "", used);
    free(out);
    free(pend);
    for (size_t i = 0; i < maxcells; ++i)
        free(cells[i].text);
    free(cells);
    free(attrs);
}

static int grab_prompt(char *linestart, int len, bool islast)
{
    isprompt_len = 0;

    sgr_prompt_start();

    /*
     * We may get a prompt in the middle of a bunch of lines, so
     * match #prompts. They usually have no #print, so we print manually
     * if islast is not set and a #prompt matches.
     */
    if ((islast || printstrlen(linestart) < cols)
        && search_prompt(linestart, true) && isprompt_len != 0) {
        /*
         * the line starts with a prompt.
         * #isprompt placed the actual prompt in $prompt,
         * we must still process the rest of the line.
         */
        if (isprompt_len > 0 && isprompt_len <= len) {
            len = isprompt_len;
            prompt_status = 1;
        }

        /*
         * following data may be the reply to a previously sent command,
         * so we may have to reprint that command.
         */
        char *reprint = reprint_getline();
        if (reprint != NULL && *reprint != '\0') {
            smart_print(promptstr, false);
            status(-1);
            tty_printf("(%s)\n", reprint);
        } else if (!islast) {
            smart_print(promptstr, true);
        }
    } else if (islast) {
        prompt->str = ptrmcpy(prompt->str, linestart, len);
        if (MEM_ERROR) {
            promptzero();
            print_error(error);
            return 0;
        }
        effective_prompt();
        prompt_status = 1; /* good, we got what to redraw */
    } else {
        len = 0;
    }

    return len;
}

/*
 * process remote input one line at time. stop at "\n".
 */
static void process_singleline(char **pbuf, int *psize)
{
    char *linestart = *pbuf, *end = *pbuf + *psize;
    char *wasn = NULL, *buf;
    char *lineend = memchr(linestart, '\n', *psize);

    if (lineend != NULL) {
        /* ok, there is a newline */
        wasn = lineend;
        *wasn = '\0';
        buf = lineend + 1; /* start of next line */
    } else {
        /* line continues till end of buffer, no trailing \n */
        lineend = end;
        buf = end;
    }

    int size = buf - linestart, len = 0;

#ifdef DEBUGCODE_2
    /* debug code to see in detail what codes come from the server */
    tty_putc('{');
    for (char *t = linestart; t < lineend && *t != '\0'; ++t) {
        char c = *t;
        if (c < ' ' || c > '~')
            tty_printf("[%d]", c);
        else
            tty_putc(c);
    }
    tty_puts("}\n");
#endif

    /*
     * Try to guess where is the prompt... really not much more than
     * a guess. Again, do it only on main connection: we do not want
     * output from other connections to mess with the prompt
     * of main connection :)
     *
     * Since we now have #prompt, behave more restrictively:
     * if no #prompts match or a #prompt matches but does not set #isprompt
     *     (i.e. recognize it for not being a prompt),
     *     we check for #actions on it when the \n arrives.
     * if a #prompt matches and sets #isprompt, then it is REALLY a prompt
     *     so never match #actions on it.
     */
    if (lineend == end && current_connection == main_connection) {
        /*
         * The last line in the chunk we received has no trailing \n
         * Assume it is a prompt.
         */
        if (isprompt_len != 0 && promptlen != 0 && prompt_status == 1) {
            draw_prompt();
            tty_putc('\n');
            col0 = 0;
            sgr_prompt_commit(promptstr, promptlen);
        }
        isprompt_len = 0;
        promptzero();
        if (lineend > linestart) {
            len = grab_prompt(linestart, lineend - linestart, true);
            if (len != 0)
                size = len;
        }
    } else {
        if (current_connection == main_connection) {
            isprompt_len = 0;
            promptzero();

            if (linestart[0] != '\0') {
                /* set $last_line */
                last_line->str = ptrmcpy(last_line->str, linestart,
                                         strlen(linestart));
                if (MEM_ERROR) {
                    print_error(error);
                    error = 0;
                }

                if (lineend > linestart) {
                    len = grab_prompt(linestart, lineend - linestart, false);
                    if (len != 0)
                        size = len;
                }
            }
        }
        if (len == 0 && (!search_action(linestart, false) || opt_autoprint)) {
            if (line0 < lines - 1)
                ++line0;
            if (current_connection != main_connection) /* sub connection */
                tty_message("##%s> ", current_connection->id);

            smart_print(linestart, true);
        }
    }

    /*
     * search_prompt and search_action above
     * might set error: clear it to avoid troubles.
     */
    error = 0;
    if (wasn != NULL)
        *wasn = '\n';
    *pbuf += size;
    *psize -= size;
}

/*
 * Code to merge lines from host that were splitted
 * into different packets: it is a horrible kludge (sigh)
 * and can be used only on one connection at time.
 * We currently do it on main connection.
 *
 * Note that this code also works for _prompts_ splitted into
 * different packets, as long as no #prompts execute #isprompt
 * on an incomplete prompt (as stated in the docs).
 */
static int process_first_fragment(char *buf, int got)
{
    int processed = 0;

    /*
     * Don't merge if the first part of the line was intercepted
     * by a #prompt action which executed #isprompt
     * (to avoid intercepting it twice)
     */
    if (*buf == '\n') {
        bool deleteprompt = false, matched = false;

        /* the text from the MUD continues after the prompt */
        sgr_prompt_commit(promptstr, promptlen);

        if (opt_compact) {
            /* in compact mode, skip the first \n */
            deleteprompt = true;
            ++processed;
        }

        /*
         * the prompt was actually a complete line.
         * no need to put it on the top of received data.
         * unless #isprompt was executed, demote it to a regular line,
         * match #actions on it, copy it in last_line.
         */
        if (isprompt_len == 0) {
            last_line->str = ptrcpy(last_line->str, prompt->str);
            if (MEM_ERROR) {
                print_error(error);
                return 0;
            }

            /*
             * Kludge for kludge: don't delete the old prompt immediately.
             * Instead, match actions on it first.
             * If it matches, clear the line before running the action
             * (done by the "true" in search_action())
             * If it doesn't match, delete it only if opt_compact != 0
             */

            matched = search_action(promptstr, true);
        }
        if (!matched)
            clear_input_line(deleteprompt);
        status(-1);
    } else {
        /*
         * try to merge the prompt with the first line in buf
         * (assuming we have a line splitted into those parts)
         * then clear the prompt.
         */
        char *spinning = NULL;

        /* find the end of the first line. include the final newline. */
        char *lineend = strchr(buf, '\n');
        if (lineend != NULL)
            ++lineend;
        else
            lineend = buf + got;

        if (isprompt_len != 0) {
            /*
             * either #isprompt _was_ executed,
             * or we got a MUME spinning bar.
             * in both cases, don't try to merge.
             *
             * print a newline (to keep the old prompt on screen)
             * only if !opt_compact and we didn't get a MUME spinning bar.
             */
            clear_input_line(opt_compact);
            if (spinning == NULL && !opt_compact) {
                tty_putc('\n');
                col0 = 0;
            }
            sgr_prompt_commit(promptstr, promptlen);
            promptzero();
        } else {
            /* ok, merge this junk with the prompt */
            ptr *p = ptrcpy(NULL, prompt->str);
            p = ptrmcat(p, buf, lineend - buf);

            if (MEM_ERROR) {
                print_error(error);
                ptrdel(&p);
                return 0;
            }
            if (p == NULL)
                return 0;
            char *dummy = ptrdata(p);
            int dummyint = ptrlen(p);
            /* this also sets last_line or prompt->str : */
            clear_input_line(true);
            process_singleline(&dummy, &dummyint);
            ptrdel(&p);

            processed = lineend - buf;
        }
    }
    return processed;
}

/*
 * process input from remote host:
 * detect special sequences, trigger actions, locate prompt,
 * word-wrap, print to tty
 */
void process_remote_input(char *buf, int size)
{
    if (promptlen != 0 && current_connection == main_connection)
        promptzero();  /* discard the prompt, we look for another one */

    status(1);

    do {
        process_singleline(&buf, &size);
    } while (size > 0);
}

static void common_clear(bool newline)
{
    clear_input_line(opt_compact);
    if (newline) {
        tty_putc('\n');
        col0 = 0;
        status(1);
    }
}

/*
 * get data from the socket and process/display it.
 */
static void get_remote_input(fd_set *read_fds, unsigned *ncache_read_fds)
{
    /*
     * tcp_read() stores up to three bytes of UTF-8 per byte read;
     * make room for reading BUFSIZE bytes at a time.  Allow for a
     * terminating \0 later.
     */
    char buffer[TCP_READ_EXPANSION * BUFSIZE + 2];
    char *buf = buffer;
    int len = 0;
    connsess *conn = current_connection;

    if (conn->fragment != NULL) {
        /* leave tcp_read() room to decode; see TCP_READ_MIN */
        len = strlen(conn->fragment);
        if (len > BUFSIZE - TCP_READ_MIN) {
            len = 0;
            common_clear(promptlen != 0 && !opt_compact);
            message_wrap(WRAP_CONT, "#error: ##%s : line too long, discarded",
                         conn->id);
        } else {
            buf += len;
            memcpy(buffer, conn->fragment, len);
        }
        free(conn->fragment);
        conn->fragment = NULL;
    }

    unsigned generation = conn->generation;
    int got = tcp_read(conn, buf, TCP_READ_EXPANSION * BUFSIZE - len);
    /* commands run by an MPI message may have closed the connection */
    if (got == 0 || conn->generation != generation)
        return;
    if (!FD_ISSET(conn->fd, read_fds)
        && conn->fns->can_read(conn)) {
        FD_SET(conn->fd, read_fds);
        ++*ncache_read_fds;
    }

    /* Safe, there is space. Do it now not to forget it later */
    buf[got] = '\0';
    received += got;

#ifdef DEBUGCODE
    /* debug code to see in detail what strange codes come from the server */
    tty_printf("%s{", edattrend);
    for (char *t = buf; t < buf + got; ++t) {
        char c = *t;
        if (c < ' ' || c > '~')
            tty_printf("[%d]", c);
        else
            tty_putc(c);
    }
    tty_puts("}\n");
#endif

    if (!conn->active)
        return; /* process only active connections */

    got += buf - buffer;
    buf = buffer;

    if (conn->spawn) {
        /* this is data from a spawned child or an attached program.
         * execute as if typed */
        current_connection = main_connection;

        char *newline = strchr(buf, '\n');
        if (newline != NULL) {
            /* instead of newline = strtok(buf, "\n") */
            *newline = '\0';

            if (opt_autoclear && line_status == 0)
                common_clear(!opt_compact);
            do {
                if (opt_info) {
                    if (line_status == 0)
                        common_clear(!opt_compact);
                    tty_message("##%s [%s]\n", conn->id, buf);
                }
                parse_user_input(buf, false);
                /* the line may have closed the connection */
                if (conn->generation != generation)
                    return;
                /*
                 * strtok() may have been used in parse_user_input()...
                 * cannot rely it refers on what we may have set above.
                 * (it causes a bug in #spawned commands if they
                 * evaluate (attr(), noattr) or they #connect ... )
                 * so do it manually.
                 */
                /*
                 * buf = strtok(NULL, "\n");
                 */
                buf = newline + 1;
                newline = strchr(buf, '\n');
                if (newline != NULL)
                    *newline = '\0';
            } while (newline != NULL);
        }

        if (*buf != '\0') {
            /*
             * save last fragment for later, when spawned command will
             * (hopefully) send the rest of the text
             */
            conn->fragment = my_strdup(buf);

            if (opt_info) {
                if (line_status == 0)
                    common_clear(!opt_compact);
                message_wrap(WRAP_CONT,
                             "#warning: ##%s: unterminated line [%s]",
                             conn->id, buf);
            }
        }
        current_connection = conn;
        return;
    }

    if (linemode & LM_CHAR) {
        /* char-by-char mode: just display output, no fuss */
        clear_input_line(false);
        sgr_output_sync();
        sgr_output(buf, got);
        tty_puts(buf);
        return;
    }

    /* line-at-a-time mode: process input in a number of ways */

    if (conn == main_connection && promptlen != 0) {
        int processed = process_first_fragment(buf, got);
        buf += processed;
        got -= processed;
    } else {
        common_clear(promptlen != 0 && !opt_compact);
    }

    if (got > 0)
        process_remote_input(buf, got);
}

#ifdef USE_REGEXP
/*
 * match a precompiled regexp, return actual params in param array;
 * return true if matched
 */
static bool match_regexp_action(void *regexp, char *line, int *match_s,
                                int *match_e)
{
    regex_t *re = regexp;
    regmatch_t reg_match[PARAM_COUNT - 1];

    if (regexec(re, line, PARAM_COUNT - 1, reg_match, 0) != 0)
        return false;

    match_s[0] = 0;
    match_e[0] = strlen(line);
    for (int n = 1; n < PARAM_COUNT; ++n) {
        match_s[n] = 0;
        match_e[n] = 0;
    }
    for (int n = 0; n <= (int)re->re_nsub && n < PARAM_COUNT - 1; ++n) {
        if (reg_match[n].rm_so == -1)
            continue;
        match_s[n + 1] = reg_match[n].rm_so;
        match_e[n + 1] = reg_match[n].rm_eo;
    }
    return true;
}
#endif

/*
 * match action containing &1..&9 and $1..$9 and return actual params
 * start/end in match_s/match_e - return true if matched
 */
static bool match_weak_action(char *pat, char *line, int *match_s,
                              int *match_e)
{
    ptr *buf = NULL;
    char *realpat = pat;

    /* unescape a copy, not the pattern of the action */
    if (!jit_subst_vars(&buf, pat))
        buf = ptrmcpy(buf, pat, strlen(pat));
    if (REAL_ERROR) {
        print_error(error);
        ptrdel(&buf);
        return false;
    }
    char empty[] = "";
    pat = buf != NULL ? ptrdata(buf) : empty;
    unescape(pat);

    for (int p = 0; p < PARAM_COUNT; ++p) {
        match_s[p] = 0;
        match_e[p] = 0;
    }

    int mbeg = 0, prm = -1;
    bool mword = false;
    if (*pat == '^') {
        ++pat;
        mbeg = 1;  /* anchor match at line start */
    }
    if (*pat == '&' || *pat == '$')
        mbeg = -mbeg - 1;  /* pattern starts with '&' or '$' */

    char *src = line, *nsrc = NULL;
    while (pat != NULL && *pat != '\0') {
        char c = *pat;
        if (c == '&' || c == '$') {
            /* &x matches a string */
            /* $x matches a single word */
            char *tmp = pat + 1;
            if (isdigit((unsigned char)*tmp)) {
                int p = 0;
                while (isdigit((unsigned char)*tmp) && p < PARAM_COUNT) {
                    p *= 10;
                    p += *tmp++ - '0';
                }
                if (p <= 0 || p >= PARAM_COUNT) {
                    ptrdel(&buf);
                    return false;
                }
                prm = p;
                pat = tmp;
                if (c == '$')
                    mword = true;
            } else {
                wrap_printf("#error: bad action pattern \"%s\"\n#missing "
                            "digit after \"%s\"", realpat, pat);
                ptrdel(&buf);
                return false;
            }
        }

        char *npat = first_valid(pat, '&');
        char *npat2 = first_valid(pat, '$');
        if (npat2 < npat)
            npat = npat2;
        /* the text to match, up to the next parameter */
        size_t mlen = npat - pat;
        if (*npat == '\0')
            npat = NULL;

        if (mlen != 0) {
            /* the text without the escapes of its $ and & */
            char end = pat[mlen];
            size_t tlen = 0;
            for (size_t i = 0; i < mlen; ++i) {
                if (pat[i] != ESC || i + 1 == mlen
                    || (pat[i + 1] != '$' && pat[i + 1] != '&'))
                    pat[tlen++] = pat[i];
            }
            pat[tlen] = '\0';
            nsrc = strstr(src, pat);
            pat[mlen] = end;
            mlen = tlen;
            if (nsrc == NULL) {
                ptrdel(&buf);
                return false;
            }
            if (mbeg > 0) {
                if (nsrc != src) {
                    ptrdel(&buf);
                    return false;
                }
                mbeg = 0;  /* reset mbeg to stop further start match */
            }
            if (prm != -1) {
                match_s[prm] = src - line;
                match_e[prm] = nsrc - line;
            }
        } else if (prm != -1) {
            /* end of pattern space */
            match_s[prm] = src - line;
            match_e[prm] = strlen(line);
        }

        /* post-processing of param */
        if (prm != -1 && match_e[prm] != 0 && mword) {
            if (mbeg == -1) {
                /* unanchored '$' start, take last word */
                char *tmp = memrchrs(line + match_s[prm],
                                     match_e[prm] - match_s[prm],
                                     DELIM, DELIM_LEN);
                if (tmp != NULL)
                    match_s[prm] = tmp - line + 1;
            } else if (*pat == '\0') {
                /* '$' at end of pattern, take first word */
                char *tmp = memchrs(line + match_s[prm],
                                    match_e[prm] - match_s[prm],
                                    DELIM, DELIM_LEN);
                if (tmp != NULL)
                    match_e[prm] = tmp - line;
            } else {
                /* match only if param is single-worded */
                if (memchrs(line + match_s[prm], match_e[prm] - match_s[prm],
                            DELIM, DELIM_LEN) != NULL) {
                    ptrdel(&buf);
                    return false;
                }
            }
        }
        if (prm != -1 && match_e[prm] != 0) {
            /* reset match flags */
            mbeg = 0;
            mword = false;
        }
        if (nsrc != NULL)
            src = nsrc + mlen;
        pat = npat;
    }
    ptrdel(&buf);

    match_s[0] = 0;
    match_e[0] = strlen(line);
    return true;
}

/*
 * does the #action or #prompt 'p' match 'line'? return the actual params
 * start/end in match_s/match_e
 */
static bool trigger_match(triggernode *p, char *line, int *match_s,
                          int *match_e)
{
    if (p->type == ACTION_WEAK)
        return match_weak_action(p->pattern, line, match_s, match_e);
#ifdef USE_REGEXP
    if (p->type == ACTION_REGEXP)
        return match_regexp_action(p->regexp, line, match_s, match_e);
#endif
    return false;
}

/*
 * Search for #actions or #prompts to trigger on an input line.
 * The line can't be trashed since we want to print it on the screen later.
 * Return true if line matched to some #action
 *
 * Optionally clear the input line before running the trigger command.
 */
static bool search_action_or_prompt(char *line, bool clearline, int onprompt)
{
    /*
     * we need actionnode and promptnode to be the same "triggernode" type
     */
    bool ret = false;
    int match_s[PARAM_COUNT], match_e[PARAM_COUNT];

    for (triggernode *p = onprompt != 0 ? prompts : actions; p != NULL;
         p = p->next) {
        if (!p->active || !trigger_match(p, line, match_s, match_e))
            continue;

        push_params();
        if (error != 0)
            return false;
        ret = true;
        error = 0;
        set_params(line, match_s, match_e);
        if (error != 0)
            return false;
        if (onprompt == 2) {
            prompt->str = ptrmcpy(prompt->str, line, strlen(line));
            if (MEM_ERROR) {
                promptzero();
                print_error(error);
                return false;
            }
        }
        if (clearline)
            clear_input_line(true);
        parse_instruction(p->command, false, true, true);
        history_done = 0;
        if (error != DYN_STACK_UND_ERROR && error != DYN_STACK_OV_ERROR)
            pop_params();
        break;
    }
    if (error != 0)
        return false;
    return ret;
}

/*
 * the length of the unbound key sequence that starts 'seq', of 'n'
 * bytes, that no binding matches although its first key starts one: for
 * ESC, ESC and a character for a meta key, or a whole CSI (ESC [) or SS3
 * (ESC O) sequence for a function key, and for another control key, such
 * as C-x, all of it; return 0 if 'seq' does not start with a control
 * key, or -1 if the sequence is incomplete
 */
static int unbound_key_len(const char *seq, int n)
{
    if (n < 2 || (unsigned char)seq[0] >= ' ')
        return 0;
    if (seq[0] != ASCII_ESC)
        return n;
    if (seq[1] == ASCII_ESC)
        return 1;
    if (seq[1] == 'O')
        return n >= 3 ? 3 : -1;
    if (seq[1] == '[') {
        /* parameter and intermediate bytes, until a final byte */
        for (int i = 2; i < n; ++i) {
            if (is_csi_final(seq[i]))
                return i + 1;
        }
        return -1;
    }
    return 2;
}

/*
 * read terminal input and send to parser.
 * decode keys that send escape sequences
 */
static void get_user_input(void)
{
    static char buf[BUFSIZE + 1];   /* allow for terminating \0 */
    char *c = buf;

    /* We have 4 possible line modes:
     * line mode, local echo: line editing functions in effect
     * line mode, no echo: sometimes used for passwords, no line editing
     * char mode, no echo: send a character directly, no local processing
     * char mode, local echo: extremely rare, do as above.
     */
    int chunk = 1;
    if (!(linemode & (LM_NOECHO | LM_CHAR))) /* line mode, local echo */
        chunk = BUFSIZE;

    int j;
    while ((j = tty_read(c, chunk)) < 0 && errno == EINTR)
        ;
    if (j == 0) {
        /* such as at the end of commands piped into powwow */
        if (tty_at_eof())
            exit_powwow();
        return;
    }

    if (j < 0 || (chunk == 1 && j != chunk))
        syserr("cannot read from the terminal");

    if (typed_shown) {
        input_hide_hint();
        typed_shown = false;
    }
    update_now();
    typed_time = now;

    c[chunk] = '\0';

    if (linemode & (LM_CHAR | LM_NOECHO))
        edit_mode_cancel();

    if (linemode & LM_CHAR) {
        /* char mode. chunk == 1; send complete UTF-8 characters */
        static char pend[UTF8_MAX];
        static int npend;
        if (*c == '\r')
            *c = '\n';     /* send Ret as before ICRNL was turned off */
        if (npend > 0 && !utf8_is_continuation(*c))
            npend = 0;          /* incomplete character */
        pend[npend++] = *c;
        if (npend >= utf8_seqlen(pend[0])) {
            tcp_write_chars(current_connection, pend, npend);
            npend = 0;
        }
        if (!(linemode & LM_NOECHO))
            tty_putc(*c);
        last_edit_cmd = NULL;
    } else if (linemode & LM_NOECHO) {
        /* sending password (line mode, no echo). chunk == 1 */
        if ((*c != '\n' && *c != '\r') && edlen < BUFSIZE - 2) {
            edbuf[edlen++] = *c;
        } else {
            edbuf[edlen] = '\0';
            if (edattrbg)
                tty_puts(edattrend);
            tcp_write(current_connection, edbuf);
            edlen = 0;
            nchars = 0;
            typed[0] = '\0';
        }
        pos = edlen;
        edbuf[pos] = '\0';
        last_edit_cmd = NULL;
    } else {
        /* normal mode (line mode, echo). chunk == BUFSIZE */
        for (; j > 0; ++c, --j) {
            /* search function key strings for match */
            /* sequences may contain \0 */
            bool done = false;
            typed[nchars++] = *c;

            while (!done) {
                done = true;
                /*
                 * shortcut:
                 * an initial single ASCII char cannot match any #bind
                 */
                keynode *p;
                if (nchars == 1 && *c >= ' ' && *c <= '~') {
                    p = NULL;
                } else {
                    for (p = keydefs;
                         (p != NULL
                          && (p->seqlen < nchars
                              || memcmp(typed, p->sequence, nchars) != 0));
                         p = p->next)
                        ;
                }

                if (p == NULL) {
                    /*
                     * ignore an unbound key sequence that starts like a bound
                     * one, as a whole once it is complete, or else type the
                     * first character; then keep processing the rest
                     */
                    if (nchars >= 2 && typed[nchars - 1] == '\b') {
                        /* a key sequence and C-h: list how it can go on */
                        clear_input_line(false);
                        tty_putc('\n');
                        char names[64];
                        key_names(typed, nchars - 1, names, sizeof(names));
                        show_binds_starting(typed, nchars - 1, names);
                        last_edit_cmd = NULL;
                        nchars = 0;
                        break;
                    }
                    int skip = unbound_key_len(typed, nchars);
                    if (skip < 0 && nchars < CAPLEN)
                        break;          /* wait for the rest */
                    last_edit_cmd = NULL;
                    if (skip != 0) {
                        if (skip < 0)
                            skip = nchars;
                        tty_putc(ASCII_BEL);
                    } else {
                        skip = 1;
                        insert_char(typed[0]);
                    }
                    int rest = nchars - skip;   /* the keys after them */
                    assert(rest >= 0);
                    memmove(typed, typed + skip, rest);
                    nchars = rest;
                    if (nchars > 0)
                        done = false;
                } else if (p->seqlen == nchars) {
                    insert_flush();
                    if (flashback)
                        putbackcursor();
                    /* the command may delete p, for example with #load */
                    function_str funct = p->funct;
                    undo_before();
                    bool handled = edit_mode_command(funct);
                    if (!handled)
                        funct(p->call_data);
                    undo_after(funct, !handled);
                    last_edit_cmd = funct;
                    nchars = 0;
                }
            }
        }
        insert_flush();
    }
}

/*
 * split str into words separated by DELIM, and place in
 * text_var(1) ... text_var(9) -
 * the whole str is put in text_var(0)
 */
static char *split_words(char *str)
{
    *text_var(0) = ptrmcpy(*text_var(0), str, strlen(str));
    for (int i = 1; i < PARAM_COUNT; ++i) {
        *num_var(i) = 0;
        ptr **prm = text_var(i);
        /* skip multiple span of DELIM */
        while (*str != '\0' && IS_DELIM(*str))
            ++str;
        char *end = str;
        while (*end != '\0' && !IS_DELIM(*end))
            ++end;
        *prm = ptrmcpy(*prm, str, end - str);
        str = end;
        if (MEM_ERROR) {
            print_error(error);
            return NULL;
        }
    }
    return str;
}

/*
 * free the whole stack and reset it to empty
 */
static void free_allparams(void)
{
    vars.depth = 0;    /* reset stack to empty */

    for (int depth = 1; depth < MAX_STACK; ++depth) {
        for (int n = 0; n < PARAM_COUNT; ++n)
            ptrdel(&vars.params[depth][n].str);
    }
}

void push_params(void)
{
    if (vars.depth == MAX_STACK - 1) {
        error = DYN_STACK_OV_ERROR;
        print_error(error);
        free_allparams();
        return;
    }
    ++vars.depth;
    for (int n = 0; n < PARAM_COUNT; ++n) {
        vars.params[vars.depth][n].num = 0;
        ptrzero(vars.params[vars.depth][n].str);
    }
}

void pop_params(void)
{
    if (vars.depth == 0) {
        error = DYN_STACK_UND_ERROR;
        print_error(error);
        free_allparams();
        return;
    }
    for (int n = 0; n < PARAM_COUNT; ++n)
        ptrdel(&vars.params[vars.depth][n].str);
    --vars.depth;
}

static void set_params(char *line, int *match_s, int *match_e)
{
    for (int i = 0; i < PARAM_COUNT; ++i) {
        *num_var(i) = 0;
        if (match_e[i] > match_s[i]) {
            *text_var(i) = ptrmcpy(*text_var(i), line + match_s[i],
                                   match_e[i] - match_s[i]);
            if (MEM_ERROR) {
                print_error(error);
                return;
            }
        } else {
            ptrzero(*text_var(i));
        }
    }
}

char *get_next_instr(char *p)
{
    p = skipspace(p);

    if (*p == '\0')
        return p;

    bool is_if = strncmp(p, "#if", 3) == 0;
    int count = is_if ? 1 : 0;

    for (;;) {
        char *sep = first_regular(p, CMDSEP);

        char *q = p;
        if (*q != '\0') {
            do {
                if (*q == '#')
                    ++q;

                q = first_regular(q, '#');
            } while (*q != '\0' && strncmp(q, "#if", 3) != 0);
        }

        if (sep <= q) {
            p = sep;
            if (*p != '\0')
                ++p;
        } else if (*q != '\0') {
            p = get_next_instr(q);
        } else {
            error = SYNTAX_ERROR;
            print_error(error);
            return NULL;
        }
        char *next = skipspace(p);
        if (*p == '\0' || count-- == 0)
            break;
        if (is_if) {
            if (strncmp(next, "#else", 5) != 0)
                break;
            p = next + 5;
            if (*p == '\0')
                break;
        }
    }

    return p;
}

static void send_line(char *line, bool silent)
{
    if (!silent && opt_echo)
        message_printf("[%s]\n", line);
    tcp_write(current_connection, line);
}

static void swap_ptrs(ptr **a, ptr **b)
{
    ptr *tmp = *a;
    *a = *b;
    *b = tmp;
}

/*
 * run 'line', an instruction that is not empty: send it to the MUD, or
 * after "##name" to the connection 'name', or run it as a block, an alias,
 * a command of powwow or a speedwalk; '*pbuf' is a buffer that it can
 * use, and '*old_conn' gets the current connection if it changes it
 */
static void run_instruction(char *line, bool silent, bool subs,
                            bool jit_subs, ptr **pbuf, connsess **old_conn)
{
    char *arg = skipspace(line);

    if (arg[0] == '#' && arg[1] == '#') {
        /* send to another connection */
        int len = strlen(arg);
        *pbuf = ptrsetlen(*pbuf, len);
        if (REAL_ERROR) {
            print_error(error);
            return;
        }
        /* the first word, the name of the connection, goes in buf */
        char *buf = ptrdata(*pbuf);
        line = split_first_word(buf, len + 1, arg + 2);
        *old_conn = current_connection;
        current_connection = tcp_find_connection(buf);
        if (current_connection == NULL) {
            error = OUT_RANGE_ERROR;
            wrap_printf("#no connection named \"%s\"", buf);
            return;
        }
        arg = skipspace(line);
        if (*arg == '\0') {
            if (current_connection->spawn) {
                error = OUT_RANGE_ERROR;
                wrap_printf("#only MUD connections can be default ones!");
            } else {
                /* set it as main connection */
                tcp_set_main(current_connection);
                *old_conn = NULL;
            }
            /* stop parsing, otherwise a newline would be sent to tcp_fd */
            return;
        }
        /* now we can trash (pbuf) */
    }

    if (*arg == '{') {
        /* a block */
        char *end = first_regular(arg + 1, '}');
        if (*end != '\0') {
            *end = '\0';
            parse_user_input(arg + 1, silent);
            *end = '}';
        } else {
            error = MISSING_PAREN_ERROR;
            print_error(error);
        }
        return;
    }

    /* initial spaces are NOT skipped this time */
    int len = strlen(line);
    *pbuf = ptrsetlen(*pbuf, len);
    if (REAL_ERROR) {
        print_error(error);
        return;
    }
    /* the first word goes in buf, and arg points to the arguments */
    char *buf = ptrdata(*pbuf);
    arg = split_first_word(buf, len + 1, line);
    bool oneword = (*arg == '\0');

    aliasnode *np = *lookup_alias(buf);
    char *end = skipspace(line);
    if (np != NULL && np->active) {
        push_params();
        if (REAL_ERROR)
            return;
        /* the arguments, split into words, go in $0 ... $9 */
        split_words(arg);
        parse_instruction(np->subst, false, true, true);
        if (error != DYN_STACK_UND_ERROR && error != DYN_STACK_OV_ERROR)
            pop_params();
    } else if (*end == '#') {
        /* a command of powwow; after aliases, so they can start with # */
        char *name = end + 1;
        end = skipspace(name);
        unsigned char first = *name;
        if (*end == '(') {
            /* #(expression) */
            ++end;
            evaln(&end);
            if (REAL_ERROR)
                print_error(error);
        } else if (first <= ASCII_MAX && ispunct(first)) {
            /* a command named by punctuation right after the #, as #!,
             * needs no space before what follows: #!ls */
            char punct[] = { (char)first, '\0' };
            parse_commands(punct, skipspace(name + 1));
        } else {
            parse_commands(buf + 1, arg);
        }
    } else if (!oneword || !map_walk(buf, silent, false)) {
        /* map_walk() takes only one word */
        if (!subs && !jit_subs)
            unescape(line);
        send_line(line, silent);
    }
}

/*
 * Parse and exec the first instruction in 'line', and return pointer to the
 * second instruction in 'line' (if any).
 */
char *parse_instruction(char *line, bool silent, bool subs, bool jit_subs)
{
    if (error != 0)
        return NULL;

    char *ret = get_next_instr(line);

    if (ret == NULL || ret == line)  /* error or empty instruction, bail out */
        return ret;

    /*
     * remove the optional ';' after an instruction,
     * to have an usable string, ending with \0.
     * If it is escaped, DON'T remove it: it is not a separator,
     * and the instruction must already end with \0, or we would not be here.
     */
    bool last_is_sep = false;
    if (ret[-1] == CMDSEP) {
        /* instruction is not empty, ret[-1] is allowed */
        if (ret > line + 1 && ret[-2] == ESC) {
            /* ';' is escaped */
        } else {
            --ret;
            *ret = '\0';
            last_is_sep = true;
        }
    }

    /*
     * using two buffers, pbuf and pbusy, for four strings:
     * result of subs_param, result of jit_subst_vars, result of
     * unescape and first word of line.
     *
     * So care is required to avoid clashes.
     */
    ptr *pbuf = NULL, *pbusy = NULL;
    bool copied = false;
    char empty[] = "";
    if (subs && subst_param(&pbuf, line)) {
        line = pbuf != NULL ? ptrdata(pbuf) : empty;
        swap_ptrs(&pbusy, &pbuf);
        copied = true;
    }
    if (jit_subs && jit_subst_vars(&pbuf, line)) {
        line = pbuf != NULL ? ptrdata(pbuf) : empty;
        swap_ptrs(&pbusy, &pbuf);
        copied = true;
    }
    if (!copied) {
        pbuf = ptrmcpy(pbuf, line, strlen(line));
        line = pbuf != NULL ? ptrdata(pbuf) : empty;
        swap_ptrs(&pbusy, &pbuf);
    }
    if (subs || jit_subs)
        unescape(line);

    /* now line is in (pbusy) and (pbuf) is available */

    /* restore integrity of original line: must still put it in history */
    if (last_is_sep) {
        *ret = CMDSEP;
        ++ret;
    }

    if (REAL_ERROR) {
        print_error(error);
        ptrdel(&pbuf);
        ptrdel(&pbusy);
        return NULL;
    }
    /* run-time debugging */
    if (opt_debug)
        message_printf("#parsing: %s\n", line);

    connsess *old_conn = NULL;

    if (*line == '\0')
        send_line(line, silent);
    else
        run_instruction(line, silent, subs, jit_subs, &pbuf, &old_conn);

    if (old_conn != NULL)
        current_connection = old_conn;
    ptrdel(&pbuf);
    ptrdel(&pbusy);
    return !REAL_ERROR ? ret : NULL;
}

/*
 * parse input from user: calls parse_instruction for each instruction
 * in cmd_line.
 * silent = true if the line should not be echoed.
 */
void parse_user_input(char *line, bool silent)
{
    do {
        line = parse_instruction(line, silent, false, false);
    } while (error == 0 && line != NULL && *line != '\0');
}

/*
 * parse powwow's own commands
 */
static void parse_commands(char *command, char *arg)
{
    /* We ALLOW special commands also on subsidiary connections ! */

    /* assume output will be enough to make input line = last screen line */
    /* line0 = lines - 1; */
    int count = isdigit((unsigned char)*command) ? atoi(command) : 0;
    if (count != 0) {
        if (count >= 0) {
            while (count-- > 0)
                parse_instruction(arg, true, false, true);
        } else {
            message_printf("#bogus repeat count\n");
        }
        return;
    }

    int len = strlen(command);
    if (len == 0) {
        /* comment */
        return;
    }

    for (cmdstruct *c = commands; c != NULL; c = c->next) {
        if (strncmp(command, c->name, len) == 0 && c->funct != NULL) {
            c->funct(arg);
            return;
        }
    }

    wrap_printf("#unknown powwow command \"%s\"", command);
}

/*
 * substitute $0..$9 and @0..@9 in a string
 * (unless $ or @ is escaped with backslash)
 *
 * return false if dst not filled. if returned false and not error,
 * there was nothing to substitute.
 */
static bool subst_param(ptr **buf, char *src)
{
    if (strchr(src, '$') == NULL && strchr(src, '@') == NULL)
        return false;

    int len = strlen(src);
    if (*buf == NULL || ptrlen(*buf) < len) {
        *buf = ptrsetlen(*buf, len);
        if (REAL_ERROR)
            return false;
    }
    char *dst = ptrdata(*buf);

    while (*src != '\0') {
        while (*src != '\0' && *src != '$' && *src != '@' && *src != ESC)
            *dst++ = *src++;

        if (*src == ESC) {
            while (*src == ESC)
                *dst++ = *src++;

            if (*src != '\0')
                *dst++ = *src++;
        }

        bool done = false;
        if (*src == '$' || *src == '@') {
            bool text = *src == '$';
            char *tmp = src + 1;
            if (isdigit((unsigned char)*tmp)) {
                int i = atoi(tmp);
                while (isdigit((unsigned char)*tmp))
                    ++tmp;

                if (i < PARAM_COUNT) {
                    int max = 0;
                    char *data = NULL, buf2[LONGLEN];

                    done = true;
                    src = tmp;

                    /* now the actual substitution */
                    if (text) {
                        if (*text_var(i) != NULL) {
                            data = ptrdata(*text_var(i));
                            max = ptrlen(*text_var(i));
                        }
                    } else {
                        data = buf2;
                        sprintf(data, "%ld", *num_var(i));
                        max = strlen(buf2);
                    }
                    if (data != NULL && max != 0) {
                        int n = dst - ptrdata(*buf);
                        *buf = ptrpad(*buf, max);
                        if (REAL_ERROR)
                            return false;
                        dst = ptrdata(*buf) + n;
                        memcpy(dst, data, max);
                        dst += max;
                    }
                }
            }
        }
        if (!done && (*src == '$' || *src == '@'))
            *dst++ = *src++;
    }
    *dst = '\0';
    return true;
}

/*
 * just-in-time substitution:
 * substitute ${name}, @{name} and #{expression} in a string
 * (unless "${", "@{" or "#{" are escaped with backslash)
 *
 * return false if dst not filled. if returned false and not error,
 * there was nothing to substitute.
 */
static bool jit_subst_vars(ptr **buf, char *src)
{
    if (strstr(src, "${") == NULL && strstr(src, "@{") == NULL
        && strstr(src, "#{") == NULL)
        return false;

    int len = strlen(src);
    if (*buf == NULL || ptrlen(*buf) < len) {
        *buf = ptrsetlen(*buf, len);
        if (REAL_ERROR)
            return false;
    }
    char *dst = ptrdata(*buf);

    while (*src != '\0') {
        while (*src != '\0' && *src != '$' && *src != '@' && *src != '#'
               && *src != ESC)
            *dst++ = *src++;

        if (*src == ESC) {
            while (*src == ESC)
                *dst++ = *src++;

            if (*src != '\0')
                *dst++ = *src++;
        }

        bool done = false;
        if (*src == '$' || *src == '@') {
            enum var_kind kind = *src == '$' ? VAR_TEXT : VAR_NUM;
            long *num = NULL;
            ptr **text = NULL;
            char *tmp = src + 1;
            if (*tmp == '{') {
                tmp = skipspace(tmp + 1);
                if (isdigit((unsigned char)*tmp) || *tmp == '-') {
                    /* numbered variable */
                    int i = atoi(tmp);
                    if (i >= -GLOBAL_VAR_COUNT && i < PARAM_COUNT) {
                        if (*tmp == '-')
                            ++tmp;
                        while (isdigit((unsigned char)*tmp))
                            ++tmp;
                        num = num_var(i);
                        text = text_var(i);
                        done = true;
                    }
                } else if (isalpha((unsigned char)*tmp) || *tmp == '_') {
                    /* named variable */
                    char *name = tmp++;
                    while (isalnum((unsigned char)*tmp) || *tmp == '_')
                        ++tmp;
                    char c = *tmp;
                    *tmp = '\0';
                    varnode *named_var = *lookup_varnode(name, kind);
                    *tmp = c;
                    if (named_var != NULL) {
                        num = &named_var->num;
                        text = &named_var->str;
                        done = true;
                    }
                }
                tmp = skipspace(tmp);
                if (*tmp != '}') {
                    /* syntax error, leave it as it is */
                    done = false;
                } else if (done) {
                    int max = 0;
                    char *data = NULL, buf2[LONGLEN];

                    src = tmp + 1; /* skip the '}' */

                    /* now the actual substitution */
                    if (kind == VAR_TEXT) {
                        if (*text != NULL) {
                            data = ptrdata(*text);
                            max = ptrlen(*text);
                        }
                    } else {
                        data = buf2;
                        sprintf(data, "%ld", *num);
                        max = strlen(buf2);
                    }
                    if (data != NULL && max != 0) {
                        int n = dst - ptrdata(*buf);
                        *buf = ptrpad(*buf, max);
                        if (REAL_ERROR)
                            return false;
                        dst = ptrdata(*buf) + n;
                        memcpy(dst, data, max);
                        dst += max;
                    }
                } else {
                    /* met an undefined variable, consider empty */
                    src = tmp + 1;
                    done = true;
                }
            }
        } else if (src[0] == '#' && src[1] == '{') {
            ptr *pbuf = NULL;

            src += 2;
            evalp(&pbuf, &src);
            if (REAL_ERROR) {
                ptrdel(&pbuf);
                return false;
            }
            if (pbuf != NULL) {
                int max = ptrlen(pbuf);
                int n = dst - ptrdata(*buf);
                *buf = ptrpad(*buf, max);
                if (REAL_ERROR) {
                    ptrdel(&pbuf);
                    return false;
                }
                dst = ptrdata(*buf) + n;
                memcpy(dst, ptrdata(pbuf), max);
                dst += max;
            }
            ptrdel(&pbuf);

            if (*src != '\0')
                src = skipspace(src);
            if (*src != '}') {
                message_printf("#{}: ");
                error = MISSING_PAREN_ERROR;
                print_error(error);
                return false;
            }
            done = true;
            if (*src != '\0')
                ++src;
        }

        if (!done && (*src == '$' || *src == '@' || *src == '#')) {
            /* not matched, just copy */
            *dst++ = *src++;
        }
    }
    *dst = '\0';
    ptrtrunc(*buf, dst - ptrdata(*buf));
    return true;
}

/*
 * set definition file:
 * rules: if powwow_dir is set it is searched first.
 *        if file doesn't exist, it is created there.
 * If a slash appears in the name, the powwow_dir isn't used.
 */
void set_deffile(char *arg)
{
    if (strchr(arg, '/') == NULL && *powwow_dir != '\0') {
        strcpy(deffile, powwow_dir);
        strcat(deffile, arg);
        if ((access(deffile, R_OK) == -1 || access(deffile, W_OK) == -1)
            && access(arg, R_OK) == 0 && access(arg, W_OK) == 0)
            strcpy(deffile, arg);
    } else {
        strcpy(deffile, arg);
    }
}

/*
 * return true if var is one of the permanent variables
 */
bool is_permanent_variable(varnode *v)
{
    return v == prompt || v == last_line;
}
