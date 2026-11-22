/* public things from log.c */

#ifndef _LOG_H_
#define _LOG_H_

#include "charset.h"

extern FILE *capturefile, *telnetfile, *pcapfile, *recordfile, *moviefile;
extern vtime movie_last;

void log_clearsleep(void);
void log_flush(void);
int log_getsize(void);
void log_resize(long newsize);
extern unsigned log_resizes;
void log_write(const char *str, int len, bool newline, enum charset cs);
void log_telnet(const void *data, size_t len);
void log_pcap_start(FILE *f);
void log_pcap(int fd, bool to_mud, const void *data, size_t len);
void log_record_line(const char *line);

void reprint_writeline(const char *line);
char *reprint_getline(void);
void reprint_clear(void);

#endif /* _LOG_H_ */

