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

char *my_strdup(const char *s);
char *my_strncpy(char *dst, const char *src, int len);
int  printstrlen(char *s);
char *utf8_fgets(char *buf, int size, FILE *f);

void sgr_output_sync(void);
void sgr_output(const char *s, size_t len);
void sgr_prompt_start(void);
const char *sgr_prompt_base(void);
void sgr_prompt_draw(void);
void sgr_prompt_commit(const char *text, size_t len);

void ptrunescape(ptr *p);
int  memunescape(char *p, int lenp, bool raw);
extern bool raw_escapes;
char *strip_end(char *s);
extern struct attr input_attr, message_attr;
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
/* like PRINTF, but wrapped as message_wrap() with WRAP_CONT */
int wrap_printf(const char *format, ...) PRINTF_FUNCTION(1, 2);

ptr  *ptrescape(ptr *dst, ptr *src, bool append);
ptr  *ptrmescape(ptr *dst, const char *src, int srclen, bool append);

ptr  *ptraddsubst_and_marks(ptr *dst, ptr *line);
ptr  *ptrmaddsubst_and_marks(ptr *dst, const char *line, int len);
ptr  *ptrsubstdef(ptr *dst, bool mbeg, char *pattern, char *replacement);

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
void suspend_powwow(int signum);
function_signal sig_permanent(int signum, function_signal sighandler);

#ifdef SA_ONESHOT
   function_signal sig_oneshot(int signum, function_signal sighandler);
#else
#  define sig_oneshot signal
#endif

void signal_start(void);
void sig_bottomhalf(void);
void errmsg(const char *format, ...) PRINTF_FUNCTION(1, 2);
void out_of_memory(void);
noreturn void syserr(const char *msg);
noreturn void exec_failed(const char *what);
int  read_settings(void);
int  save_settings(void);
void movie_write(char *str, int newline);

void update_now(void);

#endif /* _UTILS_H_ */

