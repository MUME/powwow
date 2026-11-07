/* public things from beam.c */

#ifndef _BEAM_H_
#define _BEAM_H_

int process_message(char *buf, int len);
void cancel_edit(editsess *sp);
void abort_edit_fd(struct connsess *conn);
void mpi_identify(struct connsess *conn);
void message_edit(char *text, int msglen, char view, char builtin);
void sig_chld_bottomhalf(void);
/* add the output fds of background editors to 'set'; return the new maximum fd */
int edit_output_fds(fd_set *set, int max_fd);
/* show the output of background editors ready in 'set'; return how many */
int edit_read_output(const fd_set *set);

extern char edit_start[];
extern char edit_end[];
extern editsess *edit_sess;

#endif /* _BEAM_H_ */

