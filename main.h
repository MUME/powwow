/*
 * main.h
 * various constants etc
 */

#ifndef _MAIN_H_
#define _MAIN_H_

/* shared functions from main.c */
void printver		__P ((void));
void status		__P ((int s));
void process_remote_input __P ((char *buf, int size));
void push_params	__P ((void));
void pop_params		__P ((void));
void prompt_set_iac     __P ((char *p));
char *parse_instruction __P ((char *line, char silent, char subs, char jit_subs));
char *get_next_instr	__P ((char *p));
void parse_user_input	__P ((char *line, char silent));
void set_deffile	__P ((char *arg));
int  is_permanent_variable __P ((varnode *v));


/* shared vars from main.c */
extern char identified;
extern int  prompt_status, line_status;
extern int  limit_mem;
extern char ready;
extern VOLATILE char confirm;
extern int  history_done;
extern int  linemode;
extern char hostname[];
extern int  portnumber;
extern char deffile[], helpfile[], copyfile[];

extern int cols, lines, cols_1;	/* terminal window size */
extern int olines;		/* previous terminal window size */
extern int line0, col0;		/* origin of input line */

extern varnode *prompt;		/* $prompt is always set */
extern ptr marked_prompt;	/* $prompt with marks added */

#define promptstr    (ptrdata(prompt->str))
#define promptlen    (ptrlen(prompt->str))
#define promptzero() (prompt_status = 0, ptrzero(prompt->str))

extern char surely_isprompt;    /* 1 if #prompt set #isprompt */
extern char edbuf[];		/* input line buffer */
extern int edlen;		/* characters in edbuf */
extern int pos;			/* cursor position in edbuf */
extern char edattrbeg[], edattrend[];
extern int edattrbg;

extern VOLATILE int sig_pending, sig_winch_got, sig_chld_got;

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
extern int a_nice;
extern keynode *keydefs;
extern delaynode *delays;
extern delaynode *dead_delays;
extern varnode *named_vars[2][MAX_HASH];
extern varnode *sortednamed_vars[2];
extern int num_named_vars[2];
extern int max_named_vars;
extern vars *var;
#define VAR (var+NUMVAR)

extern ptr globptr[];
extern char globptrok;
#define TAKE_PTR(pbuf, buf) do { if (globptrok & 1) globptrok &= ~1, pbuf = globptr; else if (globptrok & 2) globptrok &= ~2, pbuf = globptr + 1; else pbuf = &buf; } while(0)
#define DROP_PTR(pbuf)      do { if (*pbuf == *globptr) globptrok |= 1; else if (*pbuf == *(globptr+1))	globptrok |= 2;	else ptrdel(*pbuf); } while(0)

extern vtime now, start_time, ref_time;
extern int now_updated;

extern char initstr[];
extern char prefixstr[];
extern char inserted_next[];
extern char flashback;
extern int excursion;
extern char verbatim;

extern char opt_exit;
extern char opt_history;
extern char opt_words;
extern char opt_compact;
extern char opt_debug;
extern char opt_wrap;
extern char echo_ext;
extern char echo_int;
extern char echo_key;
extern char opt_speedwalk;
extern char opt_autoprint;
extern char opt_reprint;
extern char opt_sendsize;
extern char opt_autoclear;

extern function_str last_edit_cmd;

extern char *delim_list[];
extern int   delim_len[];
extern char *delim_name[];
extern int   delim_mode;

extern char action_chars[];

#endif /* _MAIN_H_ */
