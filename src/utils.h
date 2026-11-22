/* public declarations from utils.c */

#ifndef _UTILS_H_
#define _UTILS_H_

#include <stdnoreturn.h>

/*
 * is 'c' the final byte of a control sequence (ESC [ ... final), as
 * opposed to a parameter or intermediate byte? (ECMA-48; independent
 * of the locale, unlike isalpha())
 */
static inline bool is_csi_final(unsigned char c)
{
    return c >= 0x40 && c <= 0x7e;
}

/* is 'c' a parameter or intermediate byte of a control sequence? */
static inline bool is_csi_param(unsigned char c)
{
    return c >= 0x20 && c <= 0x3f;
}

static inline bool is_odigit(char c)
{
    return c >= '0' && c <= '7';
}

noreturn void fatal_out_of_memory(void);
void *xmalloc(size_t size);
void *xcalloc(size_t count, size_t size);
void *xrealloc(void *p, size_t size);
char *xstrdup(const char *s);
char *xstrdup_or_null(const char *s);
char *my_strncpy(char *dst, const char *src, int len);
int printstrlen(char *s);
char *utf8_fgets(char *buf, int size, FILE *f);

void sgr_output_sync(void);
void sgr_output(const char *s, size_t len);
void sgr_prompt_start(void);
const char *sgr_prompt_base(void);
void sgr_prompt_draw(void);
void sgr_prompt_commit(const char *text, size_t len);

void ptrunescape(ptr *p);
int memunescape(char *p, int lenp, bool raw);
extern bool raw_escapes;
char *strip_end(char *s);
const char *file_name(const char *name, char *buf, size_t size);
bool file_name_allowed(const char *name);
const char *windows_path(const char *path, char *buf, size_t size);
bool in_path(const char *name);
bool is_path_sep(char c);
bool has_path_sep(const char *path);
const char *env_name(const char *name, char *buf, size_t size);
/* an attribute of #hilite: the user-wide one, of the user-wide settings
 * file, or else powwow's own, and the MUD's own, if it has one, which wins
 * over it */
struct hilite {
    struct attr user, mud, def;
    bool own;
};
extern struct hilite input_hilite, message_hilite;
const struct attr *hilite_attr(const struct hilite *h);
extern char msgattrbeg[], msgattrend[];

/*
 * like tty_message(), but wrapped to the width of the terminal: before
 * the words that do not fit, which go on a new line after 'cont'; and
 * with a newline at the end
 */
int message_wrap(const char *cont, const char *format, ...)
    PRINTF_FUNCTION(2, 3);
/* the start of the lines that continue a message */
#define WRAP_CONT "#    "
/* text that powwow wraps itself is at most this wide, for readability */
enum { WRAP_COLUMNS = 80 };
int wrap_width(void);
/* like message_printf(), but wrapped as message_wrap() with WRAP_CONT */
int wrap_printf(const char *format, ...) PRINTF_FUNCTION(1, 2);

ptr *ptrescape(ptr *dst, ptr *src, bool append);
ptr *ptrmescape(ptr *dst, const char *src, int srclen, bool append);

ptr *ptraddsubst_and_marks(ptr *dst, ptr *line);
ptr *ptrmaddsubst_and_marks(ptr *dst, const char *line, int len);
ptr *ptrsubstdef(ptr *dst, bool mbeg, char *pattern, char *replacement);

void put_marks(char *dst, char *line);
void smart_print(const char *line, bool newline);
char *split_first_word(char *dst, int dstlen, char *src);
char *first_valid(const char *p, char ch);
char *first_regular(const char *p, char c);
#define first_valid(p, ch) CONST_GENERIC((p), (first_valid)((p), (ch)))
#define first_regular(p, c) CONST_GENERIC((p), (first_regular)((p), (c)))
void unescape(char *s);
void escape_specials(char *dst, const char *src);
char *skipspace(const char *p);
#define skipspace(p) CONST_GENERIC((p), (skipspace)(p))
noreturn void exit_powwow(void);

/* lock the definition file as set_deffile() sets it, as sessions do */
extern bool deffile_locking;
/* the definition file is in use by another powwow, whose pid is
 * deffile_owner, or 0 if unknown: it is not saved on exit */
extern bool deffile_busy;
extern long deffile_owner;
bool lock_deffile(void);
void unlock_deffile(void);

/* the user-wide settings file: save it at once after a change, and read it
 * again if another program wrote it */
void config_changed(void);
void config_refresh(bool at_once);
/* reading the user-wide settings file, whose #hilite is user-wide */
extern bool reading_config;
void suspend_powwow(int signum);
function_signal sig_permanent(int signum, function_signal sighandler);

#ifdef SA_ONESHOT
   function_signal sig_oneshot(int signum, function_signal sighandler);
#else
#  define sig_oneshot signal
#endif

void signal_start(void);
int sig_wake_fd(void);
void sig_wake_clear(void);
void sig_bottomhalf(void);
void errmsg(const char *format, ...) PRINTF_FUNCTION(1, 2);
noreturn void syserr(const char *msg);
noreturn void exec_failed(const char *what);
int read_settings(void);
int read_config(void);
int save_settings(void);
void movie_write(char *str, int newline);

void update_now(void);

#endif /* _UTILS_H_ */

