/* public things from log.c */

#ifndef _LOG_H_
#define _LOG_H_

enum linetype { EMPTY = 0, LINE = 1, PROMPT = 2, SLEEP = 3 };

extern FILE *capturefile, *recordfile, *moviefile;
extern vtime movie_last;

void log_clearsleep(void);
void log_flush(void);
int  log_getsize(void);
void log_resize(int newsize);
void log_write(char *str, int len, int newline);

void  reprint_writeline(char *line);
char *reprint_getline(void);
void  reprint_clear(void);

#endif /* _LOG_H_ */

