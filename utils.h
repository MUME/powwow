/* public declarations from utils.c */

#ifndef _UTILS_H_
#define _UTILS_H_

char *my_strdup		__P ((char *s));
char *my_strncpy	__P ((char *dst, char *src, int len));
int  printstrlen	__P ((char *s));

void ptrunescape __P ((ptr p));
int  memunescape __P ((char *p, int lenp));

ptr  ptrescape	 __P ((ptr dst, ptr src, int append));
ptr  ptrmescape	 __P ((ptr dst, char *src, int srclen, int append));

ptr  ptraddmarks  __P ((ptr dst, ptr line));
ptr  ptrmaddmarks __P ((ptr dst, char *line, int len));

void put_marks		__P ((char *dst, char *line));
void smart_print	__P ((char *line, char newline));
char *split_first_word  __P ((char *dst, int dstlen, char *src));
char *first_valid	__P ((char *p, char ch));
char *first_regular	__P ((char *p, char c));
void unescape		__P ((char *s));
void escape_specials	__P ((char *str, char *p));
char *skipspace		__P ((char *p));
void exit_powwow	__P ((void));
void suspend_powwow	__P ((int signum));
function_signal sig_permanent	__P ((int signum, function_signal sighandler));

#ifdef SA_ONESHOT
   function_signal sig_oneshot	__P ((int signum, function_signal sighandler));
#else
#  define sig_oneshot signal
#endif

void signal_start	__P ((void));
void sig_bottomhalf	__P ((void));
void errmsg		__P ((char *msg));
void syserr		__P ((char *msg));
int  read_settings	__P ((void));
int  save_settings	__P ((void));
void movie_write	__P ((char *str, int newline));

void update_now		__P ((void));

#endif /* _UTILS_H_ */

