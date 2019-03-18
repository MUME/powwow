/* public things from tcp.c */

#ifndef _TCP_H_
#define _TCP_H_

extern int tcp_fd;		/* current socket file descriptor */
extern int tcp_main_fd;		/* main session socket */
extern int tcp_max_fd;		/* highest used fd */

extern int tcp_count;		/* number of open connections */
extern int tcp_attachcount;	/* number of spawned or attached commands */

extern int conn_max_index;	/* 1 + highest used conn_list[] index */


/* multiple connections control */

/* state of telnet connection */
#define NORMAL		0
#define ALTNORMAL	1
#define GOT_N		2
#define GOT_R		3
#define GOTIAC		4
#define GOTWILL		5
#define GOTWONT		6
#define GOTDO		7
#define GOTDONT		8
#define GOTSB		9
#define GOTSBIAC       10

/* connection flags: */
/* ACTIVE:	display remote output	*/
/* SPAWN:	spawned cmd, not a mud	*/
/* IDEDITOR:	sent #request editor	*/
/* IDPROMPT:	sent #request prompt	*/
#define ACTIVE	 1
#define SPAWN	 2
#define IDEDITOR 4
#define IDPROMPT 8

typedef struct {
    char *id;			/* session id */
    char *host;			/* address of remote host */
    int port;			/* port number of remote host */
    int fd;			/* fd number */
    char *fragment;		/* for SPAWN connections: unprocessed text */
    char flags;
    char state;
    char old_state;
} connsess;

extern connsess conn_list[MAX_CONNECTS];     /* connection list */

extern byte conn_table[MAX_FDSCAN];	     /* fd -> index translation table */

#define CONN_LIST(n) conn_list[conn_table[n]]
#define CONN_INDEX(n) conn_list[n]

extern fd_set fdset;               /* set of descriptors to select() on */

int  tcp_connect(const char *addr, int port);
int  tcp_read(int fd, char *buffer, int maxsize);
void tcp_raw_write(int fd, const char *data, int len);
void tcp_write_escape_iac(int fd, const char *data, int len);
void tcp_write_tty_size(void);
void tcp_write(int fd, char *data);
void tcp_main_write(char *data);
void tcp_flush(void);
void tcp_set_main(int fd);
void tcp_open(char *id, char *initstring, char *host, int port);
int  tcp_find(char *id);
void tcp_show(void);
void tcp_close(char *id);
void tcp_togglesnoop(char *id);
void tcp_spawn(char *id, char *cmd);
int  tcp_unIAC(char *data, int len);

#endif /* _TCP_H_ */

