/* public definitions from tty.c */

#ifndef _TTY_H_
#define _TTY_H_

extern int tty_read_fd;

extern char *tty_clreoln, *tty_clreoscr, *tty_begoln,
            *tty_modebold, *tty_modeblink, *tty_modeuline,
	    *tty_modenorm, *tty_modenormbackup,
            *tty_modeinv, *tty_modestandon, *tty_modestandoff;

void tty_bootstrap	__P ((void));
void tty_start		__P ((void));
void tty_quit		__P ((void));
void tty_special_keys	__P ((void));
void tty_sig_winch_bottomhalf	__P ((void));
void tty_add_walk_binds		__P ((void));
void tty_add_initial_binds	__P ((void));
void tty_gotoxy			__P ((int col, int line));
void tty_gotoxy_opt		__P ((int fromcol, int fromline, int tocol, int toline));

void input_delete_nofollow_chars	__P ((int n));
void input_overtype_follow		__P ((char c));
void input_insert_follow_chars		__P ((char *str, int n));
void input_moveto			__P ((int new_pos));

#ifndef USE_LOCALE 

#define tty_puts(s) fputs((s), stdout)
/* printf("%s", (s)); would be as good */


#define tty_putc(c)             putc((unsigned char)(c), stdout)
#define tty_printf              printf
#define tty_read(buf, cnt)      read(tty_read_fd, (buf), (cnt))
#define tty_gets(s,size)        fgets((s), (size), stdin)
#define tty_flush()             fflush(stdout)
#define tty_raw_write(s,size)   do { tty_flush(); write(1, (s), (size)); } while (0)

#else /* USE_LOCALE */

#ifdef __GNUC__
 #define PRINTF_FUNCTION(string, first) \
	__attribute__ ((format (printf, string, first)))
#else
 #define PRINTF_FUNCTION(string, first)
#endif

void tty_puts           __P ((const char *s));
void tty_putc           __P ((char c));
void tty_printf         __P ((const char *format, ...)) PRINTF_FUNCTION(1, 2);
int  tty_read           __P ((char *buf, size_t count));
void tty_gets           __P ((char *s, int size));
void tty_flush          __P ((void));
void tty_raw_write      __P ((char *data, size_t len));
int  tty_has_chars      __P ((void));

#endif /* USE_LOCALE */

#endif /* _TTY_H_ */
