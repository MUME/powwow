/* public things from log.c */

#ifndef _LOG_H_
#define _LOG_H_

enum linetype { EMPTY = 0, LINE = 1, PROMPT = 2, SLEEP = 3 };

extern FILE *capturefile, *recordfile, *moviefile;
extern vtime movie_last;

void log_clearsleep	__P ((void));
void log_flush		__P ((void));
int  log_getsize	__P ((void));
void log_resize		__P ((int newsize));
void log_write		__P ((char *str, int len, int newline));

void  reprint_writeline	__P ((char *line));
char *reprint_getline	__P ((void));
void  reprint_clear	__P ((void));

#endif /* _LOG_H_ */

