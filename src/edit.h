/* public things from edit.c */

#ifndef _EDIT_H_
#define _EDIT_H_

typedef struct {
  const char *name;
  function_str funct;
  const char *group;            /* for #bind functions: the group */
  const char *help;             /* and the description */
} edit_function;

extern const edit_function internal_functions[];

/*
 * completion list:
 *     words[wordindex] is where the next word will go (always empty)
 *     words[wordindex].prev is last (least interesting)
 *     words[wordindex].next is 2nd (first to search for completion)
 */
#define WORD_UNIQUE	1		/* word is unique in list */
typedef struct {
    char *word;
    int  next, prev;
    char flags;
} wordnode;

extern char *hist[MAX_HIST];
extern int curline;
extern int pickline;

extern wordnode words[MAX_WORDS];
extern int wordindex;

/*         public function declarations         */
void edit_bootstrap(void);

int  lookup_edit_name(char *name, char **arg);
int  lookup_edit_function(function_str funct);
void draw_prompt(void);
void clear_input_line(bool deleteprompt);
void draw_input_line(void);
void redraw_line(char *dummy);
void redraw_line_noprompt(char *dummy);
void transpose_words(char *dummy);
void transpose_chars(char *dummy);
void suspend_key(char *dummy);
void kill_to_eol(char *dummy);
void kill_to_bol(char *dummy);
void yank(char *dummy);
void yank_pop(char *dummy);
void yank_last_word(char *dummy);
void undo(char *dummy);
void redo(char *dummy);
void undo_before(void);
void undo_after(function_str funct, bool ran);
void begin_of_history(char *dummy);
void end_of_history(char *dummy);
void reverse_search(char *dummy);
void forward_search(char *dummy);
void abort_edit(char *dummy);
void insert_named_char(char *dummy);
void insert_emoji(char *dummy);
bool edit_mode_command(function_str funct);
void edit_mode_cancel(void);
void end_of_line(char *dummy);
void begin_of_line(char *dummy);
void del_char_right(char *dummy);
void del_char_left(char *dummy);
void to_history(char *dummy);
void put_history(char *str);
void complete_word(char *dummy);
void complete_line(char *dummy);
void put_word(char *s);
void put_static_word(char *s);
/* show 'label' instead of the input line until input_label_end() */
void input_label_start(const char *label);
void input_label_show(void);
void input_label_end(void);
/* whether a line is typed without being shown, whether the keys are for
 * it and not for reading the name of a character for it, and the keys */
bool hidden_typing(void);
bool hidden_keys(void);
void hidden_insert(char c);
void hidden_command(function_str funct);
/* delete the history lines and completion words that contain a secret */
void purge_secrets(void);
void set_custom_delimeters(char *s);
void to_input_line(char *str);
void clear_line(char *dummy);
void enter_line(char *dummy);
void putbackcursor(void);
void insert_char(char c);
void insert_flush(void);
void next_word(char *dummy);
void prev_word(char *dummy);
void del_word_right(char *dummy);
void del_word_left(char *dummy);
void del_unix_word_left(char *dummy);
void upcase_word(char *dummy);
void downcase_word(char *dummy);
void capitalize_word(char *dummy);
void prev_line(char *dummy);
void next_line(char *dummy);
void prev_char(char *dummy);
void next_char(char *dummy);
void key_run_command(char *cmd);

void fmt_next_edit(const char *fmt, ...) PRINTF_FUNCTION(1, 2);

#endif /* _EDIT_H_ */
