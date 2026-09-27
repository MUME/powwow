/* public things from log.c */

#ifndef _LOG_H_
#define _LOG_H_

#include "charset.h"

enum linetype { EMPTY = 0, LINE = 1, PROMPT = 2, SLEEP = 3 };

extern FILE *capturefile, *recordfile, *moviefile;
extern vtime movie_last;

void log_clearsleep(void);
void log_flush(void);
int  log_getsize(void);
void log_resize(int newsize);
void log_write(const char *str, int len, int newline, enum charset cs);
void log_record_line(const char *line);

void  reprint_writeline(const char *line);
char *reprint_getline(void);
void  reprint_clear(void);

#endif /* _LOG_H_ */

