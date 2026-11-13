/* public definitions from tty.c */

#ifndef _TTY_H_
#define _TTY_H_

extern int tty_read_fd;

extern const char *tty_clreoln, *tty_clreoscr, *tty_begoln;
extern const char *tty_modebold, *tty_modeblink, *tty_modeuline;
extern const char *tty_modenorm, *tty_modeinv;
extern const char *tty_modestandon, *tty_modestandoff;

void tty_bootstrap(void);
bool tty_locale_utf8(void);
void tty_start(void);
void tty_quit(void);
void tty_special_keys(void);
const char *tty_intr_key(void);
void tty_sig_winch_bottomhalf(void);
void tty_add_walk_binds(void);
void tty_add_initial_binds(void);
void tty_add_new_binds(int version);
/*
 * a default key binding: an editing function, or the command 'command' if
 * 'funct' is key_run_command; 'since' is the save-file version that added it
 */
typedef struct {
    const char *label, *seq;
    function_str funct;
    const char *command;
    int since;
} default_bind;

const default_bind *tty_default_bind(const char *label);

/* the save-file version that added the readline-like editing keys */
enum { READLINE_KEYS_SAVEFILEVER = 9 };
void tty_gotoxy(int col, int line);
void tty_gotoxy_opt(int fromcol, int fromline, int tocol, int toline);
bool tty_cell_fits(int col, const char *s, size_t n, int w);

void input_delete_nofollow_chars(int n);
void input_insert_follow_chars(char *str, int n);
void input_replace(int from, int len, const char *str, int n);
void input_moveto(int new_pos);
void input_show_hint(const char *hint);
void input_hide_hint(void);
void input_draw(void);
int  input_line(int p);
int  input_col(int p);

void tty_puts(const char *s);
void tty_putc(char c);
int  tty_printf(const char *format, ...) PRINTF_FUNCTION(1, 2);
int  tty_read(char *buf, size_t count);
void tty_gets(char *s, int size);
void tty_flush(void);
void tty_raw_write(char *data, size_t len);
bool tty_has_chars(void);
bool tty_at_eof(void);

#endif /* _TTY_H_ */
