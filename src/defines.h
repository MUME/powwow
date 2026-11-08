/*
 * common definition and typedefs
 */

#ifndef _DEFINES_H_
#define _DEFINES_H_

#include "../config.h"

#include <assert.h>
#include <limits.h>
#include <stdbool.h>

#include "attr.h"

#include <sys/select.h>
#include <sys/time.h>

#define uSEC_PER_SEC 1000000L         /* microseconds in a second */
#define mSEC_PER_SEC 1000L            /* milliseconds in a second */
#define uSEC_PER_mSEC 1000L           /* microseconds in a millisecond */

#undef MIN2
#undef MAX2
#define MIN2(a,b) ((a)<(b) ? (a) : (b))
#define MAX2(a,b) ((a)>(b) ? (a) : (b))

#ifdef __GNUC__
 #define PRINTF_FUNCTION(string, first) \
	__attribute__ ((format (printf, string, first)))
#else
 #define PRINTF_FUNCTION(string, first)
#endif

#define PRINTF status(1), tty_printf

#define INTLEN		(3*(1+(int)sizeof(int)))
				/* max length of a string representation
				 * of an int */
#define LONGLEN		(3*(1+(int)sizeof(long)))
				/* max length of a string representation
				 * of a long */
#define ESC		'\\'	/* special escape char;
				 * update doc/powwow.md.in if changed */
#define STRESC		"\\"
#define ESC2		'`'	/* other special escape char;
				 * update doc/powwow.md.in if changed */
#define STRESC2		"`"
#define CMDSEP		';'	/* command separator character;
				 * update doc/powwow.md.in if changed */
#define SPECIAL_CHARS	"{}();\"=" /* specials chars needing escape;
				    * update doc/powwow.md.in if changed */
#define MPI		"~$#E"	/* MUME protocol introducer */
#define MPILEN		4	/* strlen(MPI) */
static_assert(sizeof(MPI) - 1 == MPILEN, "MPILEN must be strlen(MPI)");

#define MAX_CONNECTS	32	/* max number of open connections. must fit in a byte;
				 * update doc/powwow.md.in if changed */
static_assert(MAX_CONNECTS <= UCHAR_MAX, "MAX_CONNECTS must fit in a byte");

#define CAPLEN		20	/* max length of a terminal capability */
#define BUFSIZE		4096	/* general buffer size;
				 * update doc/powwow.md.in if changed */
#define PARAMLEN	99	/* initial length of text strings */
#define MAX_MAPLEN	1000	/* maximum length of automapped path;
				 * update doc/powwow.md.in if changed */
#define MIN_WORDLEN	3	/* the minimum length for history words */
#define MAX_WORDS	4096	/* number of words kept for TAB-completion;
				 * update doc/powwow.md.in if changed */
#define MAX_HIST	2048	/* number of history lines kept;
				 * update doc/powwow.md.in if changed */
#define LOG_MAX_HASH	7
#define MAX_HASH	(1<<LOG_MAX_HASH) /* max hash value, must be a power of 2 */
#define NUMPARAM	10	/* number of local unnamed params allowed
				 * (hardcoded, don't change);
				 * update doc/powwow.md.in if changed */
#define NUMVAR		50	/* number of global unnamed variables;
				 * update doc/powwow.md.in if changed */
#define NUMTOT		(NUMVAR+NUMPARAM)
#define MAX_SUBOPT	256	/* max length of suboption string */
#define MAX_ARGS	16	/* max number of arguments to editor */
#define FLASHDELAY	500	/* time of parentheses flash in millisecs */
#define KBD_TIMEOUT	100	/* timeout for keyboard read in millisecs;
				 * hope it's enough also for very slow lines */

#define MAX_STACK	100	/* maximum number of nested
                                 * action, alias, #for or #while;
                                 * update doc/powwow.md.in if changed */
#define MAX_LOOP	10000	/* maximum number of iterations in
                                 * #for or #while;
                                 * update doc/powwow.md.in if changed */

#define ACTION_WEAK	0	/* GH: normal junk */
#define ACTION_REGEXP	1	/*     oh-so-mighty regexp */
#define ACTION_TYPES	(ACTION_REGEXP + 1)

/* GH: the redefinable delimeters */
#define DELIM		(delim_list[delim_mode])
#define DELIM_LEN	(delim_len[delim_mode])
#define IS_DELIM(c)	(memchr(DELIM, (c), DELIM_LEN) != NULL)

#define DELIM_NORMAL	0	/* GH: normal word delimeters	*/
#define DELIM_PROGRAM	1	/*     ()[]{}.,;"'+/-*%		*/
#define DELIM_CUSTOM	2	/*     user-defined		*/
#define DELIM_MODES	(DELIM_CUSTOM + 1)


/* macros to find cursor position from input buffer position */
/* screen line and column of byte offset 'pos' of the input line */
#define CURLINE(pos)	input_line(pos)
#define CURCOL(pos)	input_col(pos)

#define CLIP(a, min, max) ((a)=(a)<(min) ? (min) : (a)>(max) ? (max) : (a))

#define ISMARKWILDCARD(c) ((c) == '&' || (c) == '$')


#define LM_NOECHO 1	/* no local echo */
#define LM_CHAR 2	/* char-by-char mode (no line editing) */





typedef unsigned char byte;


typedef void (*function_int)(int i);

typedef function_int function_signal;

typedef void (*function_str)(char *arg);

typedef struct timeval vtime;   /* needs #include <sys/tyme.h> */

#include "ptr.h"




/* generic linked list node (never actually created) */
typedef struct defnode {
    struct defnode *next;
    char *sortfield;
} defnode;

/*
 * twin linked list node: used to build pair of parallel lists,
 * one sorted and one not, with the same nodes
 */
typedef struct sortednode {
    struct sortednode *next;
    char *sortfield;
    struct sortednode *snext;
} sortednode;

/*
 * linked list nodes: keep "next" first, then string to sort by,
 * then (eventually) `snext'
 */
typedef struct aliasnode {
    struct aliasnode *next;
    char *name;
    struct aliasnode *snext;
    char *subst;
    char *group;
    int active;
} aliasnode;

typedef struct basenode
{
    char *pattern;
    char *start, *end; /* temporary data: start/end of current line match */
    char mbeg;
    char wild;
} basenode;

typedef struct marknode {
    struct marknode *next;
    basenode b;
    struct attr attr;
} marknode;

typedef struct substnode
{
    struct substnode *next;
    basenode b;
    char *replacement;
} substnode;

typedef struct triggernode {
    struct triggernode *next;
    char *command, *label;
    int active;
    int type;				/* GH: allow regexp */
    char *pattern;
#ifdef USE_REGEXP
    void *regexp;			/* 0 if type == ACTION_WEAK */
#endif
    char *group;
} triggernode;

/*
 * HACK WARNING :
 * actionnode and promptnode must be the same type
 * or search_action_or_prompt() in main.c won't work.
 */
typedef triggernode actionnode;
typedef triggernode promptnode;

typedef int  (*function_sort)(defnode *node1, defnode *node2);

typedef struct keynode {
    struct keynode *next;
    char *name;			/* key name */
    char *sequence;		/* escape sequence sent by terminal */
    int seqlen;			/* GH: length of esc seq to allow \0 in seq */
    function_str funct;		/* function called when key pressed */
    char *call_data;		/* data passed to function */
} keynode;

typedef struct delaynode {
    struct delaynode *next;
    char *name;
    char *command;
    vtime when;                 /* structure containing time when */
				/* command must be executed */
} delaynode;

/* Variable struct definitions */

typedef struct varnode {        /* for named variables */
    struct varnode *next;
    char *name;
    struct varnode *snext;
    int index;
    long num;
    ptr  *str;
} varnode;

typedef struct {                /* for unnamed vars */
    long num;
    ptr  *str;
} unnamedvar;

typedef struct {		/* stack of local vars */
    unnamedvar p[MAX_STACK][NUMPARAM];
    int curr;
} param_stack;

typedef struct {		/* pointers to all variables */
    long *num;
    ptr  **str;
} vars;

/* editing session control */
typedef struct editsess {
    struct editsess *next;
    unsigned int key;	/* session identifier */
    int pid;		/* pid of child */
    struct connsess *conn; /* MUD conncetion to talk with (NULL if non-MUD text) */
    char *descr;  	/* short description of what we are editing */
    char *file;		/* name of temporary file */
    time_t ctime; 	/* time when temp file was created (upper bound) */
    long oldsize; 	/* original file size */
    char cancel;	/* 1 if cancelled */
    int out_fd;		/* output of a background editor, or -1 */
    size_t out_len;	/* characters in out_buf */
    char out_buf[1024];	/* output not shown yet */
} editsess;

struct tls_options {
    bool use_tls;
    bool no_verify_ca;          /* disable certificate verification */
    char *hostname;             /* expected hostname */
};

#endif /* _DEFINES_H_ */

