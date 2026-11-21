/* public things from tcp.c */

#ifndef _TCP_H_
#define _TCP_H_

#include <stdbool.h>

#include "defines.h"
#include "charset.h"

extern int tcp_max_fd;          /* highest used fd */

extern int tcp_count;           /* number of open connections */
extern int tcp_attachcount;     /* number of spawned or attached commands */

extern int conn_max_index;      /* 1 + highest used conn_list[] index */


/* multiple connections control */

/* the state of reading a connection: its telnet commands and line ends */
enum telnet_state {
    TELNET_NORMAL,
    TELNET_ALT_NORMAL,          /* as normal, but after LF CR, where no MPI
                                 * message may start */
    TELNET_GOT_LF,
    TELNET_GOT_CR,
    TELNET_GOT_IAC,
    TELNET_GOT_WILL,
    TELNET_GOT_WONT,
    TELNET_GOT_DO,
    TELNET_GOT_DONT,
    TELNET_GOT_SB,
    TELNET_GOT_SB_IAC,
};

/* telnet CHARSET option state (RFC 2066):                   */
/* CS_WILL_SENT:   we sent WILL CHARSET                      */
/* CS_DO_RCVD:     got DO CHARSET; we may send REQUEST       */
/* CS_SRV_WILL:    got WILL CHARSET; server may send REQUEST */
/* CS_REQ_PENDING: our REQUEST awaits an answer              */
/* CS_REQ_SENT:    we sent a REQUEST on this connection      */
#define CS_WILL_SENT     1
#define CS_DO_RCVD       2
#define CS_SRV_WILL      4
#define CS_REQ_PENDING   8
#define CS_REQ_SENT     16

struct io_fns {
    ssize_t (*read)(struct connsess *conn, void *buf, size_t count);
    ssize_t (*write)(struct connsess *conn, const void *buf, size_t count);
    int (*close)(struct connsess *conn);
    bool (*can_read)(struct connsess *conn);
};

/*
 * longest start of an MPI message header kept from one read to the next:
 * the introducer, the type, 10 digits of the length and \r
 */
#define MPI_HEAD_MAX (MPILEN + 12)

typedef struct connsess {
    char *id;                   /* session id */
    unsigned generation;        /* changes when the connection closes */
    char *host;                 /* address of remote host */
    int fd;
    int port;                   /* port number of remote host */
    struct tls *tls;            /* NULL unless TLS in use */
    const struct io_fns *fns;
    char *fragment;             /* for SPAWN connections: unprocessed text */
    bool active;                /* display what it sends */
    bool spawn;                 /* a command that #spawn runs, not a MUD */
    bool ideditor;              /* sent #request editor */
    bool ideditor_utf8;         /* and announced UTF-8 support in it */
    bool naws;                  /* the MUD asked for the window size */
    bool telnet;                /* the MUD sent a telnet command */
    vtime last_sent;            /* when powwow last sent to it */
    enum telnet_state state;
    enum telnet_state old_state;
    unsigned char subopt[MAX_SUBOPT]; /* telnet suboption being read */
    int subchars;               /* and its length so far */
    char mpi_head[MPI_HEAD_MAX]; /* start of an MPI message header */
    int mpi_headlen;            /* that ended the last read, or 0 */
    bool midline;               /* the last read ended inside a line */
    bool prompt_kept;           /* the prompt before an MPI message was
                                 * kept, and no text came after it */
    bool ga_prompt;             /* the last text read was a prompt that
                                 * a telnet GA ended */
    char *rest;                 /* the bytes read after that GA, kept for
                                 * the next read, BUFSIZE of room */
    int restlen;                /* and how many, or 0 */
    bool ended;                 /* a read found the end, to report */
    int end_errno;              /* and its error, or 0 */
    unsigned char cs_flags;     /* CHARSET option state */
    bool prefer_utf8;           /* starts as and prefers UTF-8 */
    enum charset charset;       /* current encoding of this link */
    struct utf8_decoder u8dec;  /* partial sequence across reads */
} connsess;

extern connsess conn_list[MAX_CONNECTS];     /* connection list */

extern connsess *main_connection;
extern connsess *current_connection;

#define CONN_INDEX(n) conn_list[n]

extern fd_set fdset;               /* set of descriptors to select() on */

/* smallest useful maxsize for tcp_read() */
#define TCP_READ_MIN 64
/* tcp_read() stores up to about this many bytes per byte read */
#define TCP_READ_EXPANSION 3
int tcp_read(connsess *conn, void *buffer, size_t maxsize);
void tcp_raw_write(connsess *conn, const void *data, size_t len);
void tcp_write_escape_iac(connsess *conn, const void *data, size_t len);
void tcp_write_chars(connsess *conn, const char *data, size_t len);
void tcp_write_tty_size(connsess *conn);
void tcp_write(connsess *conn, const char *data);
void tcp_main_write(const char *data);
int tcp_keepalive(void);
void tcp_flush(void);
void tcp_set_main(connsess *conn);
bool tcp_open(const char *id, char *initstring, const char *host,
              int port, const struct tls_options *tlsopt, bool utf8);
void tcp_set_charset(connsess *conn, enum charset cs, const char *why);
connsess *tcp_find_connection(const char *id);
void tcp_show(void);
void tcp_close(const char *id);
void tcp_togglesnoop(const char *id);
void tcp_spawn(const char *id, char *cmd);
int tcp_unIAC(char *buffer, int len);

#endif /* _TCP_H_ */

