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
 * The original program Cancan, written by Mattias Engdeg�rd (Yorick)
 * (f91-men@nada.kth.se) 1992-94,
 * was greatly improved upon by Vivriel, Thuzzle and Ilie and then
 * transformed from Cancan into Powwow by Cosmos who worked
 * to make it yet more powerful.
 * AmigaDOS porting attempt by Fror.
 * Many new features added by Dain.
 * As usual, all the developers are in debt to countless users
 * for suggestions and debugging.
 * Maintance was taken over by Steve Slaven (bpk@hoopajoo.net) in 2005
 */

/*
 * Set this to whatever you like
 *
 * #define POWWOW_DIR	"/home/gustav/powwow"
 */

#ifdef USE_LOCALE
  #define POWWOW_HACKERS "Yorick, Vivriel, Thuzzle, Ilie, Fr\363r, D\341in"
  #define COPYRIGHT      "\251"
#else
  #define POWWOW_HACKERS "Yorick, Vivriel, Thuzzle, Ilie, Fror, Dain"
  #define COPYRIGHT      "Copyright"
#endif

#define POWWOW_VERSION VERSION                          \
    ", " COPYRIGHT " 2000-2005 by Cosmos\n"             \
    COPYRIGHT " 2005 by bpk - http://hoopajoo.net\n"    \
    "(contributions by " POWWOW_HACKERS ")\n"

#define HELPNAME "powwow.help"
#define COPYNAME "COPYING"

#ifndef POWWOW_DIR
# define POWWOW_DIR "./"
#endif

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

#ifdef USE_LOCALE
#include <locale.h>
#endif

/* are these really needed? */
extern int errno;
extern int select();

#include "defines.h"
#include "main.h"
#include "feature/regex.h"
#include "utils.h"
#include "beam.h"
#include "cmd.h"
#include "cmd2.h"
#include "edit.h"
#include "map.h"
#include "list.h"
#include "tcp.h"
#include "tty.h"
#include "eval.h"
#include "log.h"

/*     local function declarations       */
#ifdef MOTDFILE
static void printmotd(void);
#endif
static void mainloop(void);
static void exec_delays(void);
static void prompt_reset_iac(void);
static void get_remote_input(void);
static void get_user_input(void);

static int  search_action_or_prompt(char *line, char clearline, char copyprompt);
#define search_action(line, clearline) search_action_or_prompt((line), (clearline), 0)
#define search_prompt(line, copyprompt) search_action_or_prompt((line), 0, (copyprompt)+1)

static void set_params(char *line, int *match_s, int *match_e);
static void parse_commands(char *command, char *arg);
static int  subst_param(ptr *buf, char *src);
static int  jit_subst_vars(ptr *buf, char *src);


/*        GLOBALS                */
static char *helpname = HELPNAME;
static char *copyname = COPYNAME;

long received = 0;	/* amount of data received from remote host */
long sent = 0;		/* amount of data sent to remote host */

volatile char confirm = 0; /* 1 if just tried to quit */
int  history_done = 0;	/* number of recursive #history commands */
int  prompt_status = 0;	/* prompt status: 0 = ready -> nothing to do;
			 *  1 if internal echo -> must redraw;
			 * -1 if something sent to MUD -> waiting for it.
			 */
int  line_status = 0;	/* input line status: 0 = ready -> nothing to do;
			 *  1 if printed something -> must redraw.
			 */

int limit_mem = 0;	/* if !=0, max len of a string or text */

char opt_echo = 1;	/* 1 if text sent to MUD must be echoed */
char opt_keyecho = 1;	/* 1 if binds must be echoed */
char opt_info = 1;	/* 0 if internal messages are suppressed */
char opt_exit = 0;	/* 1 to autoquit when closing last conn. */
char opt_history;	/* 1 if to save also history */
char opt_words = 0;	/* 1 if to save also word completion list */
char opt_compact = 0;	/* 1 if to clear prompt between remote messages */
char opt_debug = 0;	/* 1 if to echo every line before executing it */
char opt_speedwalk = 0;	/* 1 = speedwalk on */
char opt_wrap = 0;	/* 1 = word wrap active */
char opt_autoprint = 0;	/* 1 = automatically #print lines matched by actions */
char opt_reprint = 0;	/* 1 = reprint sent commands when we get a prompt */
char opt_sendsize = 0;	/* 1 = send term size upon connect */
char opt_autoclear = 1;	/* 1 = clear input line before executing commands
			 * from spawned programs.
			 * if 0, spawned progs must #clear before printing
			 */

char hostname[BUFSIZE];
int portnumber;
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
int a_nice = 0;                    /* default priority of new actions/marks/substitutions */
keynode *keydefs;                  /* head of key binding list */
delaynode *delays;                 /* head of delayed commands list */
delaynode *dead_delays;            /* head of dead-delayed commands list */

varnode *named_vars[2][MAX_HASH];  /* head of named variables hash list */
varnode *sortednamed_vars[2];	   /* head of (ASCII) sorted named variables list */
int max_named_vars = 100;	   /* max number of named vars (grows as needed) */
int num_named_vars[2];		   /* number of named variables actually used */

static param_stack paramstk;	   /* stack of local unnamed vars */
static unnamedvar global_var[NUMTOT]; /* global unnamed vars */

vars *var;			   /* vector of all vars */

ptr globptr[2];			   /* global ptr buffer */
char globptrok = 1|2;		   /* x&i = 0 if globptr[i] is in use */

varnode *prompt;		   /* $prompt is always set */
ptr marked_prompt;		   /* $prompt with marks added */
static varnode *last_line;	   /* $line is always set to
				    * the last line processed */

vtime now;                         /* current time */
int now_updated;		   /* current time is up to date */
vtime start_time;                  /* time of powwow timer startup */
vtime ref_time;                    /* time corresponding to timer == 0 */

function_any last_edit_cmd;	   /* GH: keep track of for repeated cmds */

clock_t start_clock, cpu_clock;

char initstr[BUFSIZE];             /* initial string to send on connect */

int linemode = 0;                  /* line mode flags (LM_* in main.h) */

/* for line editing */
int cols=80, lines=24;    /* screen size */
int cols_1=79;		  /* == cols if tty_wrapglitch, == cols-1 otherwise */
int olines;		  /* previous screen size */
int col0;                 /* input line offset (= printstrlen of prompt) */
int line0;                /* screen line where the input line starts */
char edbuf[BUFSIZE];      /* line editing buffer */
int edlen;                /* length of current input line */
int pos = 0;              /* cursor position in line */
char surely_isprompt = 0; /* !=0 if last #prompt set #isprompt */
char verbatim = 0;        /* 1 = don't expand aliases or process semicolons */
char prefixstr[BUFSIZE];  /* inserted in the editing buffer each time */
char inserted_next[BUFSIZE];/* inserted in buffer just once */
char flashback = 0;	  /* cursor is on excursion and should be put back */
int excursion;		  /* where the excursion is */
char edattrbeg[CAPLEN];   /* starting input line attributes */
char edattrend[CAPLEN];   /* ending input line attributes */
int  edattrbg;		  /* input line attributes do change bg color */

/* signals handling */
volatile int sig_pending, sig_winch_got, sig_chld_got;


/* GH: different ID characters for different action types */
/*
 * Cosmos: they are hardcoded in cmd2.c, function parse_action()
 * so don't change them.
 */
char action_chars[ACTION_TYPES] = { '>', '%' };

/* GH: different delimeter modes */
char *delim_list[] = { " ;", " <>!=(),;\"'{}[]+-/*%", 0 };
int   delim_len [] = {   2 ,                     21 , 0 };
char *delim_name[] = { "normal",    "program", "custom" };
int   delim_mode = DELIM_NORMAL;

/* Group delimiter */
char *group_delim;

int main(int argc, char **argv)
{
    char *p;
    int i;
    int read_file = 0;	/* GH: if true, powwow was started with
			 * a file argument, and initstr shall be ran */

#ifdef USE_LOCALE
    if (!setlocale(LC_ALL, "")) {
        fprintf(stderr, "Failed setlocale(LC_ALL, \"\")\n");
    }
#endif

    /* initializations */
    initstr[0] = 0;
    memzero(conn_list, sizeof(conn_list));
    group_delim = my_strdup( "@" );

    update_now();
    ref_time = start_time = movie_last = now;

    start_clock = cpu_clock = clock();
#ifndef NO_RANDOM
    init_random((int)now.tv_sec);
#endif

    initialize_cmd();

    if ((p = getenv("POWWOWDIR"))) {
	strcpy(powwow_dir, p);
	if (powwow_dir[strlen(powwow_dir) - 1] != '/')
	    strcat(powwow_dir, "/");
    } else
	powwow_dir[0] = '\0';

    if ((p = getenv("POWWOWHELP")))
	strcpy(helpfile, p);
    else if (powwow_dir[0])
	strcpy(helpfile, powwow_dir);
    else
	strcpy(helpfile, POWWOW_DIR);

    if (helpfile[strlen(helpfile) - 1] != '/')
	strcat(helpfile, "/");
    strcat(helpfile, helpname);
    if (access(helpfile, R_OK) == -1 && !access(helpname, R_OK))
	strcpy(helpfile, helpname);

    if (powwow_dir[0])
	strcpy(copyfile, powwow_dir);
    else
	strcpy(copyfile, POWWOW_DIR);
    if (copyfile[strlen(copyfile) - 1] != '/')
	strcat(copyfile, "/");
    strcat(copyfile, copyname);
    if (access(copyfile, R_OK) == -1 && !access(copyname, R_OK))
	strcpy(copyfile, copyname);

    /* initialize variables */
    if ((var = (vars *)malloc(sizeof(vars)*(NUMTOT+max_named_vars)))) {
	for (i=0; i<NUMTOT; i++) {
	    var[i].num = &global_var[i].num;
	    var[i].str = &global_var[i].str;
	}
    } else
	syserr("malloc");

    /* stack is empty */
    paramstk.curr = 0;

    /* allocate permanent variables */
    if ((prompt = add_varnode("prompt", 1))
	&& (prompt->str = ptrnew(PARAMLEN))
	&& (marked_prompt = ptrnew(PARAMLEN))
	&& (last_line = add_varnode("last_line", 1))
	&& (last_line->str = ptrnew(PARAMLEN))
	&& (globptr[0] = ptrnew(PARAMLEN))
	&& (globptr[1] = ptrnew(PARAMLEN))
	&& !MEM_ERROR)
	;
    else
	syserr("malloc");


    /*
     ptr_bootstrap();
     utils_bootstrap();
     beam_bootstrap();
     cmd_bootstrap();
     map_bootstrap();
     eval_bootstrap();
     list_bootstrap();
     tcp_bootstrap();
     */
    edit_bootstrap();
    tty_bootstrap();

#ifdef MOTDFILE
    printmotd();
#endif

    printver();

    if (argc == 1) {
	tty_printf(
"\nPowwow comes with ABSOLUTELY NO WARRANTY; for details type \"#help warranty\".\n\
This is free software, and you are welcome to redistribute it\n\
under certain conditions; type \"#help copyright\" for details.\n"
		   );
    }

    if (argc == 1 || argc == 3) {
	tty_add_initial_binds();
	tty_add_walk_binds();
    } else if (argc == 2 || argc == 4) {
        /*
         * assuming first arg is definition file name
         * If three args, first is definition file name,
         * second and third are hostname and port number
         * (they overwrite the ones in definition file)
         */
        set_deffile(argv[1]);

        if (access(deffile,R_OK) == -1 || access(deffile,W_OK) == -1) {
	    char portnum[INTLEN];
            tty_printf("Creating %s\nHost name  :", deffile);
	    tty_flush();
	    tty_gets(hostname, BUFSIZE);
	    if (hostname[0] == '\n')
		hostname[0] = '\0';
	    else
		strtok(hostname, "\n");
            tty_puts("Port number:");
	    tty_flush();
	    tty_gets(portnum, INTLEN);
	    portnumber = atoi(portnum);
            tty_add_initial_binds();
	    tty_add_walk_binds();
	    limit_mem = 1048576;
            if (save_settings() < 0)
		exit(0);
        } else if (read_settings() < 0)
	    exit(0);
	else
	    read_file = 1;
    }
    if (argc == 3 || argc == 4) {
        /* assume last two args are hostname and port number */
        my_strncpy(hostname, argv[argc - 2], BUFSIZE-1);
        portnumber = atoi(argv[argc - 1]);
    }

    signal_start();
    tty_start();

    tty_puts(tty_clreoscr);
    tty_putc('\n');
    tty_gotoxy(col0 = 0, lines - 2);
    tty_puts("Type #help for help.\n");
    line0 = lines - 1;

    FD_ZERO(&fdset);
    FD_SET(tty_read_fd, &fdset);

    if (*hostname)
	tcp_open("main", (*initstr ? initstr : NULL), hostname, portnumber);

    if (read_file && !*hostname && *initstr) {
	parse_instruction(initstr, 0, 0, 1);
	history_done = 0;
    }

    confirm = 0;

    mainloop();

    /* NOTREACHED */
    return 0;
}

/*
 * show current version
 */
void printver(void)
{
    tty_printf("Powwow version %s\nOptions: %s%s\n", POWWOW_VERSION,
#ifdef USE_VT100
	       "vt100-only,"
#else
	       "termcaps,"
#endif
#ifdef USE_SGTTY
	       " BSD sgtty,"
#else
	       " termios,"
#endif
#ifdef USE_REGEXP
          " regexp "
  #ifdef USE_REGEXP_PCREPOSIX
          "(pcreposix)"
  #else
          "(libc)"
  #endif
          ","
#else
          " no regexp,"
#endif
#ifdef USE_LOCALE
	       " locale,"
#endif
#ifdef HAVE_LIBDL
	       " modules,"
#endif
	       ,
#if __STDC__
	       " compiled " __TIME__ " " __DATE__
#else
	       " uknown compile date"
#endif
	       );
}

#ifdef MOTDFILE
/*
 * print the message of the day if present
 */
static void printmotd(void)
{
    char line[BUFSIZE];
    FILE *f = fopen(MOTDFILE, "r");
    if (f) {
        while (fgets(line, BUFSIZE, f))
	    tty_puts(line);
	fclose(f);
    }
}
#endif

static void redraw_everything(void)
{
    if (prompt_status == 1 && line_status == 0)
	line_status = 1;
    if (prompt_status == 1)
	draw_prompt();
    else if (prompt_status == -1) {
	promptzero();
	col0 = surely_isprompt = '\0';
    }
    if (line_status == 1)
	draw_input_line();
}

/* how much can we sleep in select() ? */
static void compute_sleeptime(vtime **timeout)
{
    static vtime tbuf;
    int sleeptime = 0;

    if (delays) {
	update_now();
	sleeptime = diff_vtime(&delays->when, &now);
	if (!sleeptime)
	    sleeptime = 1;    /* if sleeptime is less than 1 millisec,
			       * set to 1 millisec */
    }
    if (flashback && (!sleeptime || sleeptime > FLASHDELAY))
	sleeptime = FLASHDELAY;

    if (sleeptime) {
	tbuf.tv_sec = sleeptime / mSEC_PER_SEC;
	tbuf.tv_usec = (sleeptime % mSEC_PER_SEC) * uSEC_PER_mSEC;
	*timeout = &tbuf;
    } else
	*timeout = (vtime *)NULL;
}

/*
 * main loop.
 */
static void mainloop(void)
{
    fd_set readfds;
    int i, err;
    vtime *timeout;

    for (;;) {
	tcp_fd = tcp_main_fd;
	exec_delays();

	do {
	    if (sig_pending)
		sig_bottomhalf(); /* this might set errno... */

	    tcp_flush();

	    if (!(pos <= edlen)) {
		PRINTF("\n#*ARGH* assertion failed (pos <= edlen): mail bpk@hoopajoo.net\n");
		pos = edlen;
	    }

	    redraw_everything();
	    tty_flush();

	    compute_sleeptime(&timeout);

	    error = now_updated = 0;

            readfds = fdset;
            err = select(tcp_max_fd+1, &readfds, NULL, NULL, timeout);

	    prompt_reset_iac();

	} while (err < 0 && errno == EINTR);

	if (err < 0 && errno != EINTR)
	    syserr("select");

	if (flashback) putbackcursor();

	/* process subsidiary and spawned connections first */
	if (tcp_count > 1 || tcp_attachcount) {
	    for (i=0; err && i<conn_max_index; i++) {
		if (CONN_INDEX(i).id && CONN_INDEX(i).fd != tcp_main_fd) {
		    tcp_fd = CONN_INDEX(i).fd;
		    if (FD_ISSET(tcp_fd, &readfds)) {
			err--;
			get_remote_input();
		    }
		}
	    }
	}
	/* and main connection last */
	if (tcp_main_fd != -1 && FD_ISSET(tcp_main_fd, &readfds)) {
	    tcp_fd = tcp_main_fd;
	    get_remote_input();
	}
	if (FD_ISSET(tty_read_fd, &readfds)) {
	    tcp_fd = tcp_main_fd;
	    confirm = 0;
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
    delaynode *dying;
    ptr *pbuf, buf = (ptr)0;

    if (!delays)
	return;

    update_now();

    if (cmp_vtime(&delays->when, &now) > 0)
	return;

    /* remember delayed command may modify the prompt and/or input line! */
    if (prompt_status == 0) {
	clear_input_line(opt_compact || !opt_info);
	if (!opt_compact && opt_info && prompt_status == 0 && promptlen) {
	    tty_putc('\n');
	    col0 = 0;
	    status(1);
	}
    }

    TAKE_PTR(pbuf, buf);

    while (delays && cmp_vtime(&delays->when, &now) <= 0) {
	dying = delays;           /* remove delayed command from active list */
	delays = dying->next;     /* and put it in the dead one  */

	add_node((defnode *)dying, (defnode **)&dead_delays, rev_time_sort);

	/* must be moved before executing delay->command
	 * and command must be copied in a buffer
	 * (can't you imagine why? The command may edit itself...)
	 */

	if (opt_info)
	    tty_printf("#now [%s]\n", dying->command);

	if (*dying->command) {

	    error = 0;

	    *pbuf = ptrmcpy(*pbuf, dying->command, strlen(dying->command));
	    if (MEM_ERROR)
		errmsg("malloc (#in/#at)");
	    else {
		parse_instruction(ptrdata(*pbuf), 0, 0, 1);
		history_done = 0;
	    }
	}
    }
    DROP_PTR(pbuf);
}


#define IAC_N 1024
static char *iac_v[IAC_N];
static int iac_f, iac_l;

static void prompt_reset_iac(void)
{
	iac_f = iac_l = 0;
}

void prompt_set_iac(char *p)
{
    if (iac_f == iac_l)
	iac_f = iac_l = 0;

    if (iac_l < IAC_N)
	iac_v[iac_l++] = p;
}

static char *prompt_get_iac(void)
{
    return iac_l > iac_f ? iac_v[iac_f] : NULL;
}


/* compute the effective prompt string; may end in \b* or \r */
static void effective_prompt(void)
{
    char *const pstr = promptstr;
    char *dst = pstr;
    const size_t len = promptlen;
    size_t pos = 0, maxpos = 0, p;
    for (p = 0; p < len; ++p) {
        char c = pstr[p];
        if (c == '\b') {
            if (pos > 0)
                --pos;
            continue;
        }
        if (c == '\r') {
            pos = 0;
            continue;
        }
        if (c == '\033'
            && len - p > 2 && pstr[p + 1] == '[' && pstr[p + 2] == 'K') {
            if (dst == pstr)
                dst = strdup(pstr);
            maxpos = pos;
            memset(dst + pos, 0, len - pos);
            p += 2;
            continue;
        }
        dst[pos++] = c;
        if (pos > maxpos)
            maxpos = pos;
    }
    if (dst != pstr) {
        memcpy(pstr, dst, pos);
        free(dst);
    }
    size_t nbs = maxpos - pos;
    if (nbs == 0)
        ;
    else if (pos == 0)
        pstr[maxpos++] = '\r';
    else {
        memset(pstr + maxpos, '\b', nbs);
        maxpos += nbs;
    }
    ptrsetlen(prompt->str, maxpos);
}

static int grab_prompt(char *linestart, int len, int islast)
{
    char *p;
    int is_iac_prompt = surely_isprompt = 0;

    /* recognize IAC GA as end-of-prompt marker */
    if ((CONN_LIST(tcp_fd).flags & IDPROMPT)) {
	if ((p = prompt_get_iac()) && p > linestart && p <= linestart+len)
	    iac_f++, is_iac_prompt = len = p - linestart;
	else if (!islast)
	    return 0;
    }

    /*
     * We may get a prompt in the middle of a bunch of lines, so
     * match #prompts. They usually have no #print, so we print manually
     * if islast is not set and a #prompt matches.
     */
    if ((is_iac_prompt || islast || printstrlen(linestart) < cols) &&
	((search_prompt(linestart, 1) && surely_isprompt) || is_iac_prompt)) {

	char *reprint;
	/*
	 * the line starts with a prompt.
	 * #isprompt placed the actual prompt in $prompt,
	 * we must still process the rest of the line.
	 */
	if (surely_isprompt > 0 && surely_isprompt <= len) {
	    len = surely_isprompt;
	    prompt_status = 1;
	} else if (!surely_isprompt && is_iac_prompt) {
            len = surely_isprompt = is_iac_prompt;
	    prompt->str = ptrmcpy(prompt->str, linestart, len);
            effective_prompt();
	    if (MEM_ERROR) { promptzero(); errmsg("malloc(prompt)"); return 0; }
	    prompt_status = 1;
	}

	/*
	 * following data may be the reply to a previously sent command,
	 * so we may have to reprint that command.
	 */
	if ((reprint = reprint_getline()) && *reprint) {
	    smart_print(promptstr, 0);
	    status(-1);
	    tty_printf("(%s)\n", reprint);
	} else if (!islast)
	    smart_print(promptstr, 1);
    } else if (islast) {
	prompt->str = ptrmcpy(prompt->str, linestart, len);
	if (MEM_ERROR) { promptzero(); errmsg("malloc(prompt)"); return 0; }
        effective_prompt();
	prompt_status = 1; /* good, we got what to redraw */
    } else
	len = 0;

    return len;
}


/*
 * process remote input one line at time. stop at "\n".
 */
static void process_singleline(char **pbuf, int *psize)
{
    int size, len = 0;
    char *wasn = 0, *buf, *linestart = *pbuf, *lineend, *end = *pbuf + *psize;

    if ((lineend = memchr(linestart, '\n', *psize))) {
	/* ok, there is a newline */

	*(wasn = lineend) = '\0';
	buf = lineend + 1; /* start of next line */
    }

    if (!lineend)
	/* line continues till end of buffer, no trailing \n */
	buf = lineend = end;

    size = buf - linestart;

#ifdef DEBUGCODE_2
    /* debug code to see in detail what codes come from the server */
    {
	char c;
	char *t;
	tty_putc('{');
	for (t = linestart; t < lineend && (c = *t); t++) {
	    if (c < ' ' || c > '~')
		tty_printf("[%d]", (int)c);
	    else
		tty_putc(c);
	}
	tty_puts("}\n");
    }
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
    if (lineend == end && tcp_fd == tcp_main_fd) {
	/*
	 * The last line in the chunk we received has no trailing \n
	 * Assume it is a prompt.
	 */
	if (surely_isprompt && promptlen && prompt_status == 1) {
	    draw_prompt(); tty_putc('\n'); col0 = 0;
	}
	surely_isprompt = 0;
	promptzero();
	if (lineend > linestart && (len = grab_prompt(linestart, lineend-linestart, 1)))
	    size = len;
    } else {
	if (tcp_fd == tcp_main_fd) {
	    surely_isprompt = 0;
	    promptzero();

	    if (linestart[0]) {
		/* set $last_line */
		last_line->str = ptrmcpy(last_line->str, linestart, strlen(linestart));
		if (MEM_ERROR) { print_error(error); return; }

		if (lineend > linestart && (len = grab_prompt(linestart, lineend-linestart, 0)))
		    size = len;
	    }
	}
	if (!len && ((!search_action(linestart, 0) || opt_autoprint))) {
	    if (line0 < lines - 1)
		line0++;
	    if (tcp_fd != tcp_main_fd)        /* sub connection */
		tty_printf("##%s> ", CONN_LIST(tcp_fd).id);

	    smart_print(linestart, 1);
	}
    }

    /*
     * search_prompt and search_action above
     * might set error: clear it to avoid troubles.
     */
    error = 0;
    if (wasn) *wasn = '\n';
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
	char deleteprompt = 0, matched = 0;

	if (opt_compact) {
	    /* in compact mode, skip the first \n */
	    deleteprompt = 1;
	    processed++;
	}

	/*
	 * the prompt was actually a complete line.
	 * no need to put it on the top of received data.
	 * unless #isprompt was executed, demote it to a regular line,
	 * match #actions on it, copy it in last_line.
	 */
	if (!surely_isprompt) {
	    last_line->str = ptrcpy(last_line->str, prompt->str);
	    if (MEM_ERROR) { print_error(error); return 0; }

	    /*
	     * Kludge for kludge: don't delete the old prompt immediately.
	     * Instead, match actions on it first.
	     * If it matches, clear the line before running the action
	     * (done by the "1" in search_action() )
	     * If it doesn't match, delete it only if opt_compact != 0
	     */

	    matched = search_action(promptstr, 1);
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
	char *lineend, *spinning = NULL;

	/* find the end of the first line. include the final newline. */
	if ((lineend = strchr(buf, '\n')))
	    lineend++;
	else
	    lineend = buf + got;

	if (surely_isprompt) {
	    /*
	     * either #isprompt _was_ executed,
	     * or we got a MUME spinning bar.
	     * in both cases, don't try to merge.
	     *
	     * print a newline (to keep the old prompt on screen)
	     * only if !opt_compact and we didn't get a MUME spinning bar.
	     */
	    clear_input_line(opt_compact);
	    if (!spinning && !opt_compact)
		tty_putc('\n'), col0 = 0;
	    promptzero();
       	} else {
	    ptr *pp, p = (ptr)0;
	    char *dummy;
	    int dummyint;

	    /* ok, merge this junk with the prompt */
	    TAKE_PTR(pp, p);
	    *pp = ptrcpy(*pp, prompt->str);
	    *pp = ptrmcat(*pp, buf, lineend - buf);

	    if (MEM_ERROR) { print_error(error); return 0; }
	    if (!*pp)
		return 0;
	    dummy = ptrdata(*pp);
	    dummyint = ptrlen(*pp);
	    /* this also sets last_line or prompt->str : */
	    clear_input_line(1);
	    process_singleline(&dummy, &dummyint);

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

    if (promptlen && tcp_fd == tcp_main_fd)
	promptzero();  /* discard the prompt, we look for another one */

    status(1);

    do {
	process_singleline(&buf, &size);
    } while (size > 0);
}

static void common_clear(int newline)
{
    clear_input_line(opt_compact);
    if (newline) {
	tty_putc('\n'); col0 = 0; status(1);
    }
}

/*
 * get data from the socket and process/display it.
 */
static void get_remote_input(void)
{
    char buffer[BUFSIZE + 2];        /* allow for a terminating \0 later */
    char *buf = buffer, *newline;
    int got, otcp_fd, i;

    if (CONN_LIST(tcp_fd).fragment) {
	if ((i = strlen(CONN_LIST(tcp_fd).fragment)) >= BUFSIZE-1) {
	    i = 0;
	    common_clear(promptlen && !opt_compact);
	    tty_printf("#error: ##%s : line too long, discarded\n", CONN_LIST(tcp_fd).id);
	} else {
	    buf += i;
	    memcpy(buffer, CONN_LIST(tcp_fd).fragment, i);
	}
	free(CONN_LIST(tcp_fd).fragment);
	CONN_LIST(tcp_fd).fragment = 0;
    } else
	i = 0;

    got = tcp_read(tcp_fd, buf, BUFSIZE - i);
    if (!got)
	return;

    buf[got]='\0';  /* Safe, there is space. Do it now not to forget it later */
    received += got;

#ifdef DEBUGCODE
    /* debug code to see in detail what strange codes come from the server */
    {
	char c, *t;
	newline = buf + got;
	tty_printf("%s{", edattrend);
	for (t = buf; t < newline; t++) {
	    if ((c = *t) < ' ' || c > '~')
		tty_printf("[%d]", c);
	    else tty_putc(c);
	}
	tty_puts("}\n");
    }
#endif

    if (!(CONN_LIST(tcp_fd).flags & ACTIVE))
	return; /* process only active connections */

    got += (buf - buffer);
    buf = buffer;

    if (CONN_LIST(tcp_fd).flags & SPAWN) {
	/* this is data from a spawned child or an attached program.
	 * execute as if typed */
	otcp_fd = tcp_fd;
	tcp_fd = tcp_main_fd;

	if ((newline = strchr(buf, '\n'))) {
	    /* instead of newline = strtok(buf, "\n") */
	    *newline = '\0';

	    if (opt_autoclear && line_status == 0) {
		common_clear(!opt_compact);
	    }
	    do {
		if (opt_info) {
		    if (line_status == 0) {
			common_clear(!opt_compact);
		    }
		    tty_printf("##%s [%s]\n", CONN_LIST(otcp_fd).id, buf);
		}
		parse_user_input(buf, 0);
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
		if ((buf = newline) &&
		    (newline = strchr(++buf, '\n')))
			*newline = '\0';
	    } while (buf && newline);
	}

	if (buf && *buf && !newline) {
	    /*
	     * save last fragment for later, when spawned command will
	     * (hopefully) send the rest of the text
	     */
	    CONN_LIST(otcp_fd).fragment = my_strdup(buf);

	    if (opt_info) {
		if (line_status == 0) {
		    common_clear(!opt_compact);
		}
		tty_printf("#warning: ##%s : unterminated [%s]\n", CONN_LIST(otcp_fd).id, buf);
	    }
	}
	tcp_fd = otcp_fd;
	return;
    }

    if (linemode & LM_CHAR) {
	/* char-by-char mode: just display output, no fuss */
	clear_input_line(0);
	tty_puts(buf);
	return;
    }

    /* line-at-a-time mode: process input in a number of ways */

    if (tcp_fd == tcp_main_fd && promptlen) {
	i = process_first_fragment(buf, got);
	buf += i, got -= i;
    } else {
	common_clear(promptlen && !opt_compact);
    }

    if (got > 0)
	process_remote_input(buf, got);
}


#ifdef USE_REGEXP
/*
 * GH: matches precompiled regexp, return actual params in param array
 *     return 1 if matched, 0 if not
 */
static int match_regexp_action(void *regexp, char *line, int *match_s, int *match_e)
{
    regmatch_t reg_match[NUMPARAM - 1];

    if (!regexec((regex_t *)regexp, line, NUMPARAM - 1, reg_match, 0)) {
	int n;

	match_s[0] = 0;
	match_e[0] = strlen(line);
	for (n = 1; n < NUMPARAM; n++)
	    match_s[n] = match_e[n] = 0;
	for (n = 0; n <= (int)((regex_t *)regexp)->re_nsub &&
	     n < NUMPARAM - 1; n++) {
	    if (reg_match[n].rm_so == -1) continue;
	    match_s[n+1] = reg_match[n].rm_so;
	    match_e[n+1] = reg_match[n].rm_eo;
	}
	return 1;
    }
    return 0;
}
#endif

/*
 * match action containing &1..&9 and $1..$9 and return actual params start/end
 * in match_s/match_e - return 1 if matched, 0 if not
 */
static int match_weak_action(char *pat, char *line, int *match_s, int *match_e)
{
    char mpat[BUFSIZE], *npat=0, *npat2=0, *src=line, *nsrc=0, c;
    ptr *pbuf, buf = (ptr)0;
    char *tmp, *realpat = pat;
    int mbeg = 0, mword = 0, prm = -1;

    TAKE_PTR(pbuf, buf);

    if (jit_subst_vars(pbuf, pat))
	pat = ptrdata(*pbuf);
    if (REAL_ERROR) {
	print_error(error);
	DROP_PTR(pbuf);
	return 0;
    }
    unescape(pat);

    {
        int p;
        for (p = 0; p < NUMPARAM; p++)
            match_s[p] = match_e[p] = 0;
    }

    if (*pat == '^') {
	pat++;
	mbeg = 1;  /* anchor match at line start */
    }
    if (*pat == '&' || *pat == '$')
	mbeg = - mbeg - 1;  /* pattern starts with '&' or '$' */

    while (pat && *pat) {
	if (((c=*pat) == '&' || c == '$')) {
	    /* &x matches a string */
	    /* $x matches a single word */
	    tmp = pat + 1;
	    if (isdigit(*tmp)) {
		int p = 0;
		while (isdigit(*tmp) && p < NUMPARAM) {
		    p *= 10;
		    p += *tmp++ - '0';
		}
		if (p <= 0 || p >= NUMPARAM) {
		    DROP_PTR(pbuf);
		    return 0;
		}
		prm = p;
		pat = tmp;
		if (c == '$')
		    mword = 1;
	    } else {
		PRINTF("#error: bad action pattern \"%s\"\n#missing digit after \"%s\"\n",
		       realpat, pat);
		DROP_PTR(pbuf);
		return 0;
	    }
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
	    if (!nsrc) {
		DROP_PTR(pbuf);
		return 0;
	    }
	    if (mbeg > 0) {
		if (nsrc != src) {
		    DROP_PTR(pbuf);
		    return 0;
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
	if (prm != -1 && match_e[prm] && mword) {
	    if (mbeg == -1) {
		/* unanchored '$' start, take last word */
		if ((tmp = memrchrs(line + match_s[prm],
				    match_e[prm] - match_s[prm],
				    DELIM, DELIM_LEN))) {
		    match_s[prm] = tmp - line + 1;
		}
	    } else if (!*pat) {
		/* '$' at end of pattern, take first word */
		if ((tmp = memchrs(line + match_s[prm],
				   match_e[prm] - match_s[prm],
				   DELIM, DELIM_LEN)))
		    match_e[prm] = tmp - line;
	    } else {
		/* match only if param is single-worded */
		if (memchrs(line + match_s[prm],
			    match_e[prm] - match_s[prm],
			    DELIM, DELIM_LEN)) {
		    DROP_PTR(pbuf);
		    return 0;
		}
	    }
	}
	if (prm != -1 && match_e[prm])
	    mbeg = mword = 0;  /* reset match flags */
	src = nsrc + strlen(mpat);
	pat = npat;
    }
    DROP_PTR(pbuf);

    match_s[0] = 0; match_e[0] = strlen(line);
    return 1;
}

/*
 * Search for #actions or #prompts to trigger on an input line.
 * The line can't be trashed since we want to print it on the screen later.
 * Return 1 if line matched to some #action, 0 otherwise
 *
 * Optionally clear the input line before running the trigger command.
 */
static int search_action_or_prompt(char *line, char clearline, char onprompt)
{
    /*
     * we need actionnode and promptnode to be the same "triggernode" type
     */
    triggernode *p;
    int ret = 0;
    int match_s[NUMPARAM], match_e[NUMPARAM];

    for (p = onprompt ? prompts : actions; p; p = p->next) {
#ifdef USE_REGEXP
        if (p->active &&
	    ((p->type == ACTION_WEAK && match_weak_action(p->pattern, line, match_s, match_e))
	     || (p->type == ACTION_REGEXP && match_regexp_action(p->regexp, line, match_s, match_e))
	     ))
#else
	    if (p->active &&
		((p->type == ACTION_WEAK && match_weak_action(p->pattern, line, match_s, match_e))
		 ))
#endif
	{
	    push_params(); if (error) return 0;
	    ret = 1; error = 0;
	    set_params(line, match_s, match_e); if (error) return 0;
	    if (onprompt == 2) {
		prompt->str = ptrmcpy(prompt->str, line, strlen(line));
		if (MEM_ERROR) { promptzero(); errmsg("malloc(prompt)"); return 0; }
	    }
	    if (clearline)
		clear_input_line(1);
	    parse_instruction(p->command, 0, 1, 1);
	    history_done = 0;
	    if (error!=DYN_STACK_UND_ERROR && error!=DYN_STACK_OV_ERROR)
		pop_params();
            break;
	}
    }
    if (error) return 0;
    return ret;
}

/*
 * read terminal input and send to parser.
 * decode keys that send escape sequences
 */
static void get_user_input(void)
{
    int i, j, chunk = 1;
    static char buf[BUFSIZE+1];   /* allow for terminating \0 */
    char *c = buf;
    static char typed[CAPLEN];    /* chars typed so far (with partial match) */
    static int nchars = 0;	  /* number of them */

    /* We have 4 possible line modes:
     * line mode, local echo: line editing functions in effect
     * line mode, no echo: sometimes used for passwords, no line editing
     * char mode, no echo: send a character directly, no local processing
     * char mode, local echo: extremely rare, do as above.
     */
    if (!(linemode & (LM_NOECHO | LM_CHAR))) /* line mode, local echo */
	chunk = BUFSIZE;

    while ((j = tty_read(c, chunk)) < 0 && errno == EINTR)
	;
    if (j == 0)
        return;

    if (j < 0 || (chunk == 1 && j != chunk))
	syserr("read from tty");

    c[chunk] = '\0';

    if (linemode & LM_CHAR) {
	/* char mode. chunk == 1 */
	while ((i = write(tcp_fd, c, 1)) < 0 && errno == EINTR)
	    ;
	if (i != 1)
	    syserr("write to socket");
	if (!(linemode & LM_NOECHO))
	    tty_putc(*c);
	last_edit_cmd = (function_any)0;
    } else if (linemode & LM_NOECHO) {
	/* sending password (line mode, no echo). chunk == 1 */
	if ((*c != '\n' && *c != '\r') && edlen < BUFSIZE - 2)
	    edbuf[edlen++] = *c;
	else {
	    edbuf[edlen] = '\0';
#ifdef BUG_ANSI
	    if (edattrbg)
		tty_puts(edattrend);
#endif
	    tcp_write(tcp_fd, edbuf);
	    edlen = 0;
	    typed[nchars = 0] = 0;
	}
	edbuf[pos = edlen] = '\0';
	last_edit_cmd = (function_any)0;
    } else {
	/* normal mode (line mode, echo). chunk == BUFSIZE */
	int done = 0;
	keynode *p;

	for (; j > 0; c++, j--) {

	    /* search function key strings for match */
	    /* GH: support for \0 in sequence */
	    done = 0;
	    typed[nchars++] = *c;

	    while (!done) {
		done = 1;
		/*
		 * shortcut:
		 * an initial single ASCII char cannot match any #bind
		 */
		if (nchars == 1 && *c >= ' ' && *c <= '~')
		    p = NULL;
		else {
		    for (p = keydefs; (p && (p->seqlen < nchars ||
					     memcmp(typed, p->sequence, nchars)));
			 p = p->next)
			;
		}

		if (!p) {
		    /*
		     * GH: type the first character and keep processing
		     *     the rest in the input buffer
		     */
		    i = 1;
		    last_edit_cmd = (function_any)0;
		    insert_char(typed[0]);
		    while (i < nchars) {
			typed[i - 1] = typed[i];
			i++;
		    }
		    if (--nchars)
			done = 0;
		} else if (p->seqlen == nchars) {
		    if (flashback)
			putbackcursor();
		    p->funct(p->call_data);
		    last_edit_cmd = (function_any)p->funct; /* GH: keep track of last command */
		    nchars = 0;
		}
	    }
	}
    }
}

/*
 * split str into words separated by DELIM, and place in
 * VAR[1].str ... VAR[9].str -
 * the whole str is put in VAR[0].str
 */
static char *split_words(char *str)
{
    int i;
    char *end;
    ptr *prm;

    *VAR[0].str = ptrmcpy(*VAR[0].str, str, strlen(str));
    for (i = 1; i < NUMPARAM; i++) {
	*VAR[i].num = 0;
	prm = VAR[i].str;
	/* skip multiple span of DELIM */
	while (*str && strchr(DELIM, *str))
	    str++;
	end = str;
	while (*end && !strchr(DELIM, *end))
	    end++;
	*prm = ptrmcpy(*prm, str, end-str);
	str = end;
	if (MEM_ERROR) { print_error(error); return NULL; }
    }
    return str;
}

/*
 * free the whole stack and reset it to empty
 */
static void free_allparams(void)
{
    int i,j;

    paramstk.curr = 0;    /* reset stack to empty */

    for (i=1; i<MAX_STACK; i++) {
	for (j=0; j<NUMPARAM; j++) {
	    ptrdel(paramstk.p[i][j].str);
	    paramstk.p[i][j].str = (ptr)0;
	}
    }
    for (j=0; j<NUMPARAM; j++) {
	VAR[j].num = &paramstk.p[0][j].num;
	VAR[j].str = &paramstk.p[0][j].str;
    }
}

void push_params(void)
{
    int i;

    if (paramstk.curr < MAX_STACK - 1)
	paramstk.curr++;
    else {
	print_error(error=DYN_STACK_OV_ERROR);
	free_allparams();
	return;
    }
    for (i=0; i<NUMPARAM; i++) {
	        *(VAR[i].num = &paramstk.p[paramstk.curr][i].num) = 0;
	ptrzero(*(VAR[i].str = &paramstk.p[paramstk.curr][i].str));
    }
}

void pop_params(void)
{
    int i;

    if (paramstk.curr > 0)
	paramstk.curr--;
    else {
	print_error(error=DYN_STACK_UND_ERROR);
	free_allparams();
	return;
    }
    for (i=0; i<NUMPARAM; i++) {
	ptrdel(*VAR[i].str); *VAR[i].str = (ptr)0;
	VAR[i].num = &paramstk.p[paramstk.curr][i].num;
	VAR[i].str = &paramstk.p[paramstk.curr][i].str;
    }
}

static void set_params(char *line, int *match_s, int *match_e)
{
    int i;

    for (i=0; i<NUMPARAM; i++) {
	*VAR[i].num = 0;
	if (match_e[i] > match_s[i]) {
	    *VAR[i].str = ptrmcpy(*VAR[i].str, line + match_s[i],
				  match_e[i] - match_s[i]);
	    if (MEM_ERROR) {
		print_error(error);
		return;
	    }
	} else
	    ptrzero(*VAR[i].str);
    }
}

char *get_next_instr(char *p)
{
    int count, is_if;
    char *sep, *q;

    p = skipspace(p);

    if (!*p)
	return p;

    count = is_if = !strncmp(p, "#if", 3);

    do {
	sep   = first_regular(p, CMDSEP);

	q = p;
	if (*q) do {
	    if (*q == '#') q++;

	    q = first_regular(q, '#');
	} while (*q && strncmp(q, "#if", 3));

	if (sep<=q) {
	    if (*(p = sep))
		p++;
	}
	else
	    if (*q)
		p = get_next_instr(q);
	else {
	    print_error(error=SYNTAX_ERROR);
	    return NULL;
	}
	sep = skipspace(p);
    } while (*p && count-- &&
	     (!is_if || (!strncmp(sep, "#else", 5) &&
			 (*(p = sep + 5)))));

    return p;
}

static void send_line(char *line, char silent)
{
    if (!silent && opt_echo) { PRINTF("[%s]\n", line); }
    tcp_write(tcp_fd, line);
}


/*
 * Parse and exec the first instruction in 'line', and return pointer to the
 * second instruction in 'line' (if any).
 */
char *parse_instruction(char *line, char silent, char subs, char jit_subs)
{
    aliasnode *np;
    char *buf, *arg, *end, *ret;
    char last_is_sep = 0;
    int len, copied = 0, otcp_fd = -1;
    ptr p1 = (ptr)0, p2 = (ptr)0;
    ptr *pbuf, *pbusy, *tmp;

    if (error) return NULL;

    ret = get_next_instr(line);

    if (!ret || ret==line)  /* error or empty instruction, bail out */
	return ret;

    /*
     * remove the optional ';' after an instruction,
     * to have an usable string, ending with \0.
     * If it is escaped, DON'T remove it: it is not a separator,
     * and the instruction must already end with \0, or we would not be here.
     */
    if (ret[-1] == CMDSEP) {
	/* instruction is not empty, ret[-1] is allowed */
	if (ret > line + 1 && ret[-2] == ESC) {
	    /* ';' is escaped */
	} else {
	    *--ret = '\0';
	    last_is_sep = 1;
	}
    }

    /*
     * using two buffers, p1 and p2, for four strings:
     * result of subs_param, result of jit_subst_vars, result of
     * unescape and first word of line.
     *
     * So care is required to avoid clashes.
     */
    TAKE_PTR(pbuf, p1);
    TAKE_PTR(pbusy, p2);

    if (subs && subst_param(pbuf, line)) {
	line = *pbuf ? ptrdata(*pbuf) : "";
	SWAP2(pbusy, pbuf, tmp);
	copied = 1;
    }
    if (jit_subs && jit_subst_vars(pbuf, line)) {
	line = *pbuf ? ptrdata(*pbuf) : "";
	SWAP2(pbusy, pbuf, tmp);
	copied = 1;
    }
    if (!copied) {
	*pbuf = ptrmcpy(*pbuf, line, strlen(line));
	line = *pbuf ? ptrdata(*pbuf) : "";
	SWAP2(pbusy, pbuf, tmp);
    }
    if (subs || jit_subs)
	unescape(line);

    /* now line is in (pbusy) and (pbuf) is available */

    /* restore integrity of original line: must still put it in history */
    if (last_is_sep)
	*ret++ = CMDSEP;

    if (REAL_ERROR) {
	print_error(error);
	DROP_PTR(pbuf); DROP_PTR(pbusy);
	return NULL;
    }
    /* run-time debugging */
    if (opt_debug) {
	PRINTF("#parsing: %s\n", line);
    }

    if (!*line)
	send_line(line, silent);
    else do {
	arg = skipspace(line);

	if (arg[0] == '#' && arg[1] == '#') { /* send to other connection */
	    *pbuf = ptrsetlen(*pbuf, len = strlen(arg));
	    if (REAL_ERROR) { print_error(error); break; }
	    buf = ptrdata(*pbuf);
	    line = split_first_word(buf, len+1, arg + 2);
	    /* now (pbuf) is used too: first word of line */
	    /* line contains the rest */
	    otcp_fd = tcp_fd;
	    if ((tcp_fd = tcp_find(buf))<0) {
		error = OUT_RANGE_ERROR;
		PRINTF("#no connection named \"%s\"\n", buf);
		break;
	    }
	    arg = skipspace(line);
	    if (!*arg) {
		if (CONN_LIST(tcp_fd).flags & SPAWN) {
		    error = OUT_RANGE_ERROR;
		    PRINTF("#only MUD connections can be default ones!\n");
		} else {
		    /* set it as main connection */
		    tcp_set_main(tcp_fd);
		    otcp_fd = -1;
		}
		/* stop parsing, otherwise a newline would be sent to tcp_fd */
		break;
	    }
	    /* now we can trash (pbuf) */
	}

	if (*arg == '{') {    /* instruction contains a block */
	    end = first_regular(line = arg + 1, '}');

	    if (*end) {
		*end = '\0';
		parse_user_input(line, silent);
		*end = '}';
	    } else
		print_error(error=MISSING_PAREN_ERROR);
	} else {
	    int oneword;
	    /* initial spaces are NOT skipped this time */

	    *pbuf = ptrsetlen(*pbuf, len = strlen(line));
	    if (REAL_ERROR) { print_error(error); break; }

	    buf = ptrdata(*pbuf);
	    arg = split_first_word(buf, len+1, line);
	    /* buf contains the first word, arg points to arguments */

	    /* now pbuf is used too */
	    if (!*arg) oneword = 1;
	    else oneword = 0;

	    if ((np = *lookup_alias(buf))&&np->active) {
		push_params();
		if (REAL_ERROR) break;

		split_words(arg);  /* split argument into words
				    and place them in $0 ... $9 */
		parse_instruction(np->subst, 0, 1, 1);

		if (error!=DYN_STACK_UND_ERROR && error!=DYN_STACK_OV_ERROR)
		    pop_params();

		/* now check for internal commands */
		/* placed here to allow also aliases starting with "#" */
	    } else if (*(end = skipspace(line)) == '#') {

		if (*(end = skipspace(end + 1)) == '(') {    /* execute #() */
		    end++;
		    (void)evaln(&end);
		    if (REAL_ERROR) print_error(error);
		} else
		    parse_commands(buf + 1, arg);
		/* ok, buf contains skipspace(first word) */
	    } else if (!oneword || !map_walk(buf, silent, 0)) {
		/* it is ok, map_walk accepts only one word */

		if (!subs && !jit_subs)
		    unescape(line);
		send_line(line, silent);
	    }
	}
    } while (0);

    if (otcp_fd != -1)
	tcp_fd = otcp_fd;
    DROP_PTR(pbuf); DROP_PTR(pbusy);
    return !REAL_ERROR ? ret : NULL;
}

/*
 * parse input from user: calls parse_instruction for each instruction
 * in cmd_line.
 * silent = 1 if the line should not be echoed, 0 otherwise.
 */
void parse_user_input(char *line, char silent)
{
    do {
	line = parse_instruction(line, silent, 0, 0);
    } while (!error && line && *line);
}

/*
 * parse powwow's own commands
 */
static void parse_commands(char *command, char *arg)
{
    int i, j;
    cmdstruct *c;

    /* We ALLOW special commands also on subsidiary connections ! */

    /* assume output will be enough to make input line = last screen line */
    /* line0 = lines - 1; */
    if (isdigit(*command) && (i = atoi(command))) {
	if (i >= 0) {
	    while (i--)
		(void)parse_instruction(arg, 1, 0, 1);
	} else {
	    PRINTF("#bogus repeat count\n");
	}
    } else {
	j = strlen(command);

	if( j == 0 ) {
		/* comment */
		return;
	}

	for( c = commands; c != NULL; c = c -> next )
	    if (!strncmp(command, c -> name, j)) {
		if (c -> funct) {
		    (*(c -> funct))(arg);
		    return;
		}
	    }

	PRINTF("#unknown powwow command \"%s\"\n", command);
    }
}

/*
 * substitute $0..$9 and @0..@9 in a string
 * (unless $ or @ is escaped with backslash)
 *
 * return 0 if dst not filled. if returned 0 and not error,
 * there was nothing to substitute.
 */
static int subst_param(ptr *buf, char *src)
{
    int done, i;
    char *dst, *tmp, kind;

    if (!strchr(src, '$') && !strchr(src, '@'))
	return 0;

    i = strlen(src);
    if (!*buf || ptrlen(*buf) < i) {
	*buf = ptrsetlen(*buf, i);
	if (REAL_ERROR)
	    return 0;
    }
    dst = ptrdata(*buf);

    while (*src)  {
	while (*src && *src != '$' && *src != '@' && *src != ESC)
	    *dst++ = *src++;

	if (*src == ESC) {
	    while (*src == ESC)
		*dst++ = *src++;

	    if (*src)
		*dst++ = *src++;
	}

	done = 0;
	if (*src == '$' || *src == '@') {
	    kind = *src == '$' ? 1 : 0;
	    tmp = src + 1;
	    if (isdigit(*tmp)) {
		i = atoi(tmp);
		while (isdigit(*tmp))
		    tmp++;

		if (i < NUMPARAM) {
		    int max = 0, n;
		    char *data = NULL, buf2[LONGLEN];

		    done = 1;
		    src = tmp;

		    /* now the actual substitution */
		    if (kind) {
			if (*VAR[i].str && (data = ptrdata(*VAR[i].str)))
			    max = ptrlen(*VAR[i].str);
		    } else {
			sprintf(data = buf2, "%ld", *VAR[i].num);
			max = strlen(buf2);
		    }
		    if (data && max) {
			n = dst - ptrdata(*buf);
			*buf = ptrpad(*buf, max);
			if (REAL_ERROR)
			    return 0;
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
    return 1;
}

/*
 * just-in-time substitution:
 * substitute ${name}, @{name} and #{expression} in a string
 * (unless "${", "@{" or "#{" are escaped with backslash)
 *
 * return 0 if dst not filled. if returned 0 and not error,
 * there was nothing to substitute.
 */

static int jit_subst_vars(ptr *buf, char *src)
{
    int i, done, kind;
    char *tmp, *name, *dst, c;
    varnode *named_var;

    if (!strstr(src, "${") && !strstr(src, "@{") && !strstr(src, "#{"))
	return 0;

    i = strlen(src);
    if (!*buf || ptrlen(*buf) < i) {
	*buf = ptrsetlen(*buf, i);
	if (REAL_ERROR)
	    return 0;
    }
    dst = ptrdata(*buf);

    while (*src)  {
	while (*src && *src != '$' && *src != '@' && *src != '#' && *src != ESC)
	    *dst++ = *src++;

	if (*src == ESC) {
	    while (*src == ESC)
		*dst++ = *src++;

	    if (*src)
		*dst++ = *src++;
	}

	done = 0;
	if (*src == '$' || *src == '@') {
	    i = 0;
	    kind = *src == '$' ? 1 : 0;
	    tmp = src + 1;
	    if (*tmp == '{') {
		tmp = skipspace(tmp+1);
		if (isdigit(*tmp) || *tmp == '-') {
		    /* numbered variable */
		    i = atoi(tmp);
		    if (i >= -NUMVAR && i < NUMPARAM) {
			if (*tmp == '-')
			    tmp++;
			while (isdigit(*tmp))
			    tmp++;
			done = 1;
		    }
		} else if (isalpha(*tmp) || *tmp == '_') {
		    /* named variable */
		    name = tmp++;
		    while (isalnum(*tmp) || *tmp == '_')
			tmp++;
		    c = *tmp;
		    *tmp = '\0';
		    named_var = *lookup_varnode(name, kind);
		    *tmp = c;
		    if (named_var) {
			i = named_var->index;
			done = 1;
		    }
		}
		tmp = skipspace(tmp);
		if (done) {
		    int max = 0, n;
		    char *data = NULL, buf2[LONGLEN];

		    src = tmp + 1; /* skip the '}' */

		    /* now the actual substitution */
		    if (kind == 1) {
			if (*VAR[i].str && (data = ptrdata(*VAR[i].str)))
			    max = ptrlen(*VAR[i].str);
		    } else {
			sprintf(data = buf2, "%ld", *VAR[i].num);
			max = strlen(buf2);
		    }
		    if (data && max) {
			n = dst - ptrdata(*buf);
			*buf = ptrpad(*buf, max);
			if (REAL_ERROR)
			    return 0;
			dst = ptrdata(*buf) + n;
			memcpy(dst, data, max);
			dst += max;
		    }
		} else if (*tmp == '}')
		    /* met an undefined variable, consider empty */
		    src = tmp + 1;

		/* else syntax error, do nothing */
	    }
	} else if (src[0] == '#' && src[1] == '{') {
	    int max, n;
	    ptr pbuf = (ptr)0;

	    src += 2;
	    (void)evalp(&pbuf, &src);
	    if (REAL_ERROR) {
		ptrdel(pbuf);
		return 0;
	    }
	    if (pbuf) {
		max = ptrlen(pbuf);
		n = dst - ptrdata(*buf);
		*buf = ptrpad(*buf, max);
		if (REAL_ERROR) {
		    ptrdel(pbuf);
		    return 0;
		}
		dst = ptrdata(*buf) + n;
		memcpy(dst, ptrdata(pbuf), max);
		dst += max;
	    }
	    ptrdel(pbuf);

	    if (*src)
		src = skipspace(src);
	    if (*src != '}') {
		PRINTF("#{}: ");
		print_error(error=MISSING_PAREN_ERROR);
		return 0;
	    }
	    done = 1;
	    if (*src)
		src++;
	}

	if (!done && (*src == '$' || *src == '@' || *src == '#'))
	    /* not matched, just copy */
	    *dst++ = *src++;
    }
    *dst = '\0';
    ptrtrunc(*buf, dst - ptrdata(*buf));
    return 1;
}

/*
 * set definition file:
 * rules: if powwow_dir is set it is searched first.
 *        if file doesn't exist, it is created there.
 * If a slash appears in the name, the powwow_dir isn't used.
 */
void set_deffile(char *arg)
{
    if (!strchr(arg, '/') && *powwow_dir) {
	strcpy(deffile, powwow_dir);
	strcat(deffile, arg);
	if ((access(deffile, R_OK) == -1 || access(deffile, W_OK) == -1)
	    && !access(arg,R_OK) && !access(arg, W_OK))
	    strcpy(deffile, arg);
    } else
	strcpy(deffile, arg);
}

/*
 * GH: return true if var is one of the permanent variables
 */
int is_permanent_variable(varnode *v)
{
    return (v == prompt || v == last_line);
}
