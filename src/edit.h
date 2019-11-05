/* public things from edit.c */

#ifndef _EDIT_H_
#define _EDIT_H_

typedef struct {
  char *name;
  function_str funct;
} edit_function;

extern edit_function internal_functions[];

/*
 * GH: completion list, stolen from cancan 2.6.3a
 *
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
void clear_input_line(int deleteprompt);
void draw_input_line(void);
void redraw_line(char *dummy);
void redraw_line_noprompt(char *dummy);
void transpose_words(char *dummy);
void transpose_chars(char *dummy);
void kill_to_eol(char *dummy);
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
void set_custom_delimeters(char *s);
void to_input_line(char *str);
void clear_line(char *dummy);
void enter_line(char *dummy);
void putbackcursor(void);
void insert_char(char c);
void next_word(char *dummy);
void prev_word(char *dummy);
void del_word_right(char *dummy);
void del_word_left(char *dummy);
void upcase_word(char *dummy);
void downcase_word(char *dummy);
void prev_line(char *dummy);
void next_line(char *dummy);
void prev_char(char *dummy);
void next_char(char *dummy);
void key_run_command(char *cmd);

#endif /* _EDIT_H_ */
