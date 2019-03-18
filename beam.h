/* public things from beam.c */

#ifndef _BEAM_H_
#define _BEAM_H_

int process_message(char *buf, int len);
void cancel_edit(editsess *sp);
void abort_edit_fd(int fd);
void message_edit(char *text, int msglen, char view, char builtin);
void sig_chld_bottomhalf(void);

extern char edit_start[];
extern char edit_end[];
extern editsess *edit_sess;

#endif /* _BEAM_H_ */

