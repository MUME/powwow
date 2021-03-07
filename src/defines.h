/*
 * common definition and typedefs
 */

#ifndef _DEFINES_H_
#define _DEFINES_H_

#if !defined(SYS_TIME_H) && !defined(_H_SYS_TIME)
#  include <sys/time.h>
#endif

#ifdef AIX
#  include <sys/select.h>
#endif

#define memzero(a,b)	memset((a), 0, (b))

#ifdef USE_RANDOM
#  define get_random random
#  define init_random srandom
#else
#  define get_random lrand48
#  define init_random srand48
#endif

#define uSEC_PER_SEC ((long)1000000)  /* microseconds in a second */
#define mSEC_PER_SEC ((long)1000)     /* milliseconds in a second */
#define uSEC_PER_mSEC ((long)1000)    /* microseconds in a millisecond */

#undef MIN2
#undef MAX2
#undef ABS
#undef SIGN
#undef SWAP2
#define MIN2(a,b) ((a)<(b) ? (a) : (b))
#define MAX2(a,b) ((a)>(b) ? (a) : (b))
#define ABS(a)    ((a)> 0  ? (a) :(-a))
#define SIGN(a)   ((a)> 0  ?  1  : (a) ? -1 : 0)
#define SWAP2(a,b,c) ((c)=(b), (b)=(a), (a)=(c))

/* macros to match parentheses */
#define ISRPAREN(c) ((c) == ')' || (c) == ']' || (c) == '}')
#define ISLPAREN(c) ((c) == '(' || (c) == '[' || (c) == '{')
#define LPAREN(c) ((c) == ')' ? '(' : ((c) == ']' ? '[' : '{'))

#define ISODIGIT(c) ((c) >= '0' && (c) <= '7')

#define PRINTF status(1), tty_printf

#define INTLEN		(3*(1+(int)sizeof(int)))
				/* max length of a string representation
				 * of an int */
#define LONGLEN		(3*(1+(int)sizeof(long)))
				/* max length of a string representation
				 * of a long */
#define ESC		'\\'	/* special escape char */
#define STRESC		"\\"
#define ESC2		'`'	/* other special escape char */
#define STRESC2		"`"
#define CMDSEP		';'	/* command separator character */
#define SPECIAL_CHARS	"{}();\"=" /* specials chars needing escape */
#define MPI		"~$#E"	/* MUME protocol introducer */
#define MPILEN		4	/* strlen(MPI) */

#ifdef NR_OPEN
# define MAX_FDSCAN	NR_OPEN
#else
# define MAX_FDSCAN	256	/* max number of fds */
#endif

#define MAX_CONNECTS	32	/* max number of open connections. must fit in a byte */

#define CAPLEN		20	/* max length of a terminal capability */
#define BUFSIZE		4096	/* general buffer size */
#define PARAMLEN	99	/* initial length of text strings */
#define MAX_MAPLEN	1000	/* maximum length of automapped path */
#define MIN_WORDLEN	3	/* the minimum length for history words */
#define MAX_WORDS	4096	/* number of words kept for TAB-completion */
#define MAX_HIST	2048	/* number of history lines kept */
#define LOG_MAX_HASH	7
#define MAX_HASH	(1<<LOG_MAX_HASH) /* max hash value, must be a power of 2 */
#define NUMPARAM	10	/* number of local unnamed params allowed
				 * (hardcoded, don't change) */
#define NUMVAR		50	/* number of global unnamed variables */
#define NUMTOT		(NUMVAR+NUMPARAM)
#define MAX_SUBOPT	256	/* max length of suboption string */
#define MAX_ARGS	16	/* max number of arguments to editor */
#define FLASHDELAY	500	/* time of parentheses flash in millisecs */
#define KBD_TIMEOUT	100	/* timeout for keyboard read in millisecs;
				 * hope it's enough also for very slow lines */

#define MAX_STACK	100	/* maximum number of nested
                                 * action, alias, #for or #while */
#define MAX_LOOP	10000	/* maximum number of iterations in
                                 * #for or #while */

#define ACTION_WEAK	0	/* GH: normal junk */
#define ACTION_REGEXP	1	/*     oh-so-mighty regexp */
#define ACTION_TYPES	(ACTION_REGEXP + 1)

/* GH: the redefinable delimeters */
#define DELIM		(delim_list[delim_mode])
#define DELIM_LEN	(delim_len[delim_mode])
#define IS_DELIM(c)	(strchr(DELIM, (c)))

#define DELIM_NORMAL	0	/* GH: normal word delimeters	*/
#define DELIM_PROGRAM	1	/*     ()[]{}.,;"'+/-*%		*/
#define DELIM_CUSTOM	2	/*     user-defined		*/
#define DELIM_MODES	(DELIM_CUSTOM + 1)


/* macros to find cursor position from input buffer position */
#define CURLINE(pos)	(((pos) + col0) / cols_1 + line0)
#define CURCOL(pos)	(((pos) + col0) % cols_1)

#define CLIP(a, min, max) ((a)=(a)<(min) ? (min) : (a)>(max) ? (max) : (a))

#define ISMARKWILDCARD(c) ((c) == '&' || (c) == '$')

/*
 * Attribute codes: bit 0-4 for foreground color, 5-9 for
 * background color, 10-13 for effects.
 * Color #16 is "none", so 0x0210 is "no attribute".
 */
#define COLORS         16         /* number of colors on HFT terminal */
#define LOWCOLORS      8          /* number of ANSI colors */
#define NO_COLOR       COLORS     /* no color change, use default */
#define BITS_COLOR     5          /* bits used for a color entry
                                     (16==none is a valid color) */
#define BITS_2COLOR    10         /* bits used for 2 color entries */
#define COLOR_MASK     0x1F       /* 5 (BITS_COLOR) bits set to 1, others 0 */

#define ATTR_BOLD      0x01
#define ATTR_BLINK     0x02
#define ATTR_UNDERLINE 0x04
#define ATTR_INVERSE   0x08

/*
 * WARNING: colors and attributes are currently using 14 bits:
 * 4 for attributes, 5 for foreground color and 5 for background.
 * type used is int and -1 is used as 'invalid attribute'
 * so in case ints are 16 bits, there is only 1 bit left unused.
 * In case ints are 32 bits, no problem.
 */

/* constructors / accessors for attribute codes */
#define ATTRCODE(attr, fg, bg) \
                (((attr) << BITS_2COLOR) | ((bg) << BITS_COLOR) | (fg))
#define FOREGROUND(attrcode) ((attrcode) & COLOR_MASK)
#define BACKGROUND(attrcode) (((attrcode) >> BITS_COLOR) & COLOR_MASK)
#define ATTR(attrcode)       ((attrcode) >> BITS_2COLOR)

#define NOATTRCODE ATTRCODE(0, NO_COLOR, NO_COLOR)

/*
 * NCSA telnet 2.2 doesn't reset the color when it receives "esc [ m",
 * so we must know what the normal colors are in order to reset it.
 * These colors can be changed with the #color command.
 */
#ifdef BUG_TELNET
# define DEFAULTFG 7		/* make default white text */
# define DEFAULTBG 4		/* on blue background */
#endif

#define LM_NOECHO 1	/* no local echo */
#define LM_CHAR 2	/* char-by-char mode (no line editing) */





typedef unsigned char byte;

typedef void (*function_any) ();	/* generic function pointer */

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

typedef struct marknode {
    struct marknode *next;
    char *pattern;
    int attrcode;
    char *start, *end;
    char mbeg;
    char wild;
} marknode;

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
    ptr  str;
} varnode;

typedef struct {                /* for unnamed vars */
    long num;
    ptr  str;
} unnamedvar;

typedef struct {		/* stack of local vars */
    unnamedvar p[MAX_STACK][NUMPARAM];
    int curr;
} param_stack;

typedef struct {		/* pointers to all variables */
    long *num;
    ptr  *str;
} vars;

/* editing session control */
typedef struct editsess {
    struct editsess *next;
    unsigned int key;	/* session identifier */
    int pid;		/* pid of child */
    int fd;		/* MUD socket to talk with (-1 if non-MUD text) */
    char *descr;  	/* short description of what we are editing */
    char *file;		/* name of temporary file */
    time_t ctime; 	/* time when temp file was created (upper bound) */
    long oldsize; 	/* original file size */
    char cancel;	/* 1 if cancelled */
} editsess;

#endif /* _DEFINES_H_ */

