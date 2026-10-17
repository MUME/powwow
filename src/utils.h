/* public declarations from utils.c */

#ifndef _UTILS_H_
#define _UTILS_H_

char *my_strdup(char *s);
char *my_strncpy(char *dst, char *src, int len);
int  printstrlen(char *s);

void ptrunescape(ptr p);
int  memunescape(char *p, int lenp);

ptr  ptrescape(ptr dst, ptr src, int append);
ptr  ptrmescape(ptr dst, char *src, int srclen, int append);

ptr  ptraddsubst_and_marks(ptr dst, ptr line);
ptr  ptrmaddsubst_and_marks(ptr dst, char *line, int len);

void put_marks(char *dst, char *line);
void smart_print(char *line, char newline);
char *split_first_word(char *dst, int dstlen, char *src);
char *first_valid(char *p, char ch);
char *first_regular(char *p, char c);
void unescape(char *s);
void escape_specials(char *str, char *p);
char *skipspace(char *p);
void exit_powwow(void);
void suspend_powwow(int signum);
function_signal sig_permanent(int signum, function_signal sighandler);

#ifdef SA_ONESHOT
   function_signal sig_oneshot(int signum, function_signal sighandler);
#else
#  define sig_oneshot signal
#endif

void signal_start(void);
void sig_bottomhalf(void);
void errmsg(char *msg);
void syserr(char *msg);
int  read_settings(void);
int  save_settings(void);
void movie_write(char *str, int newline);

void update_now(void);

#endif /* _UTILS_H_ */
