/* public things from tcp.c */

#ifndef _TCP_H_
#define _TCP_H_

#include <stdbool.h>

#include "defines.h"
#include "charset.h"

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
/* IDEDITOR_UTF8: last #request editor announced UTF-8 support */
#define ACTIVE	 1
#define SPAWN	 2
#define IDEDITOR 4
#define IDPROMPT 8
#define IDEDITOR_UTF8 16

/* telnet CHARSET option state (RFC 2066): */
/* CS_WILL_SENT:   we sent WILL CHARSET			*/
/* CS_DO_RCVD:     got DO CHARSET; we may send REQUEST	*/
/* CS_SRV_WILL:    got WILL CHARSET; server may send REQUEST	*/
/* CS_REQ_PENDING: our REQUEST awaits an answer		*/
/* CS_REQ_SENT:    we sent a REQUEST on this connection	*/
#define CS_WILL_SENT	 1
#define CS_DO_RCVD	 2
#define CS_SRV_WILL	 4
#define CS_REQ_PENDING	 8
#define CS_REQ_SENT	16

struct io_fns {
    ssize_t (*read)(struct connsess *conn, void *buf, size_t count);
    ssize_t (*write)(struct connsess *conn, const void *buf, size_t count);
    int (*close)(struct connsess *conn);
    bool (*can_read)(struct connsess *conn);
};

typedef struct connsess {
    char *id;			/* session id */
    char *host;			/* address of remote host */
    int fd;
    int port;			/* port number of remote host */
    struct tls *tls;            /* NULL unless TLS in use */
    const struct io_fns *fns;
    char *fragment;		/* for SPAWN connections: unprocessed text */
    char flags;
    char state;
    char old_state;
    unsigned char cs_flags;	/* CHARSET option state */
    bool prefer_utf8;		/* --utf8 given for this connection */
    enum charset charset;	/* current encoding of this link */
    struct utf8_decoder u8dec;	/* partial sequence across reads */
} connsess;

extern connsess conn_list[MAX_CONNECTS];     /* connection list */

extern connsess *main_connection;
extern connsess *current_connection;

#define CONN_INDEX(n) conn_list[n]

extern fd_set fdset;               /* set of descriptors to select() on */

int  tcp_read(connsess *conn, void *buffer, size_t maxsize);
void tcp_raw_write(connsess *conn, const void *data, size_t len);
void tcp_write_escape_iac(connsess *conn, const void *data, size_t len);
void tcp_write_tty_size(void);
void tcp_write(connsess *conn, const char *data);
void tcp_main_write(const char *data);
void tcp_flush(void);
void tcp_set_main(connsess *conn);
void tcp_open(const char *id, char *initstring, const char *host,
              int port, const struct tls_options *tlsopt, bool utf8);
void tcp_set_charset(connsess *conn, enum charset cs, const char *why);
connsess *tcp_find_connection(const char *id);
void tcp_show(void);
void tcp_close(const char *id);
void tcp_togglesnoop(const char *id);
void tcp_spawn(const char *id, char *cmd);
int  tcp_unIAC(char *data, int len);

#endif /* _TCP_H_ */

