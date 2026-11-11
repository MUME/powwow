/*
 * main.h
 * various constants etc
 */

#ifndef _MAIN_H_
#define _MAIN_H_

#include <stdbool.h>

#include "defines.h"

struct varnode;

/* shared functions from main.c */
/* print the version line (level 0), also the address of the project (1),
 * and also the authors and the compile options (2) */
void printver(int level);
void status(int s);
void process_remote_input(char *buf, int size);
void push_params(void);
void pop_params(void);
void prompt_set_iac(char *p);
char *parse_instruction(char *line, bool silent, bool subs,
                        bool jit_subs);
char *get_next_instr(char *p);
void parse_user_input(char *line, bool silent);
void set_deffile(char *arg);
bool is_permanent_variable(struct varnode *v);
/* the variable @n or $n, for n from -GLOBAL_VAR_COUNT to PARAM_COUNT - 1 */
long *num_var(int n);
ptr **text_var(int n);


/* shared vars from main.c */
extern int  prompt_status, line_status;
extern int  limit_mem;
extern char ready;
extern volatile char confirm;
extern int  history_done;
extern int  linemode;

struct hostinfo {
    char name[BUFSIZE];
    int port;
    struct tls_options tlsopt;
    bool utf8;			/* UTF-8, else ISO-8859-1 */
};
extern struct hostinfo hostinfo;
/* the charset of connections without --utf8 or --latin1: UTF-8, unless
 * powwow was started with --latin1 */
extern bool default_utf8;
/* whether connections without --tls or --no-tls use TLS: yes, unless
 * powwow was started with --no-tls or built without TLS support */
extern bool default_tls;

#define UTF8_FLAG   "--utf8"
#define LATIN1_FLAG "--latin1"
#define charset_flag_str(utf8) ((utf8) ? " " UTF8_FLAG : " " LATIN1_FLAG)

extern char deffile[], helpfile[], copyfile[];

extern int cols, lines, cols_1;	/* terminal window size */
extern int olines;		/* previous terminal window size */
extern int line0, col0;		/* origin of input line */

extern varnode *prompt;		/* $prompt is always set */
extern ptr *marked_prompt;	/* $prompt with marks added */

#define promptstr    (ptrdata(prompt->str))
#define promptlen    (ptrlen(prompt->str))
#define promptzero() (prompt_status = 0, ptrzero(prompt->str))

extern int isprompt_len;        /* see main.c */
extern char edbuf[];		/* input line buffer */
extern int edlen;		/* characters in edbuf */
extern int pos;			/* cursor position in edbuf */
extern int edstart;		/* start of the editable text in edbuf */
extern int edlabel;		/* length of a label at the start of edbuf */
extern char edattrbeg[], edattrend[];
extern bool edattrbg;

extern volatile int sig_pending, sig_winch_got, sig_chld_got;

extern long received, sent;

#ifndef NO_CLOCK
#include <time.h>
extern clock_t start_clock, cpu_clock;
#endif

extern aliasnode *aliases[MAX_HASH];
extern aliasnode *sortedaliases;
extern actionnode *actions;
extern promptnode *prompts;
extern marknode *markers;
extern substnode *substitutions;
extern int a_nice;
extern keynode *keydefs;
extern delaynode *delays;
extern delaynode *dead_delays;
extern struct named_vars named_vars[VAR_KINDS];

extern vtime now, start_time, ref_time;
extern int now_updated;

extern char initstr[];
extern char prefixstr[];
extern char inserted_next[BUFSIZE];
extern char flashback;
extern int excursion;
extern bool verbatim;

extern bool opt_exit;
extern bool opt_history;
extern bool opt_words;
extern bool opt_compact;
extern bool opt_debug;
extern bool opt_wrap;
extern bool opt_echo;
extern bool opt_info;
extern bool opt_keyecho;
extern bool opt_speedwalk;
extern bool opt_autoprint;
extern bool opt_reprint;
extern bool opt_sendsize;
extern bool opt_autoclear;

/* the commands that #option editor, pager and shell set, or NULL */
extern char *opt_editor, *opt_pager, *opt_shell;

extern function_str last_edit_cmd;

extern const char *delim_list[];
extern int   delim_len[];
extern const char *const delim_name[];
extern int   delim_mode;

/* Group delimiter for actions/aliases, defaults to @ */
extern char *group_delim;

extern const char action_chars[];

#endif /* _MAIN_H_ */
