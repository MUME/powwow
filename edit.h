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
#define	WORD_RETAIN	2		/* permanent (#command) */
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
void edit_bootstrap	  __P ((void));

int  lookup_edit_name	  __P ((char *name, char **arg));
int  lookup_edit_function __P ((function_str funct));
void draw_prompt	  __P ((void));
void clear_input_line	  __P ((int deleteprompt));
void draw_input_line	  __P ((void));
void redraw_line	  __P ((char *dummy));
void redraw_line_noprompt __P ((char *dummy));
void transpose_words	__P ((char *dummy));
void transpose_chars	__P ((char *dummy));
void kill_to_eol	__P ((char *dummy));
void end_of_line	__P ((char *dummy));
void begin_of_line	__P ((char *dummy));
void del_char_right	__P ((char *dummy));
void del_char_left	__P ((char *dummy));
void to_history		__P ((char *dummy));
void put_history	__P ((char *str));
void complete_word	__P ((char *dummy));
void complete_line	__P ((char *dummy));
void put_word		__P ((char *s));
void set_custom_delimeters __P ((char *s));
void to_input_line	__P ((char *str));
void clear_line		__P ((char *dummy));
void enter_line		__P ((char *dummy));
void putbackcursor	__P ((void));
void insert_char	__P ((char c));
void next_word		__P ((char *dummy));
void prev_word		__P ((char *dummy));
void del_word_right	__P ((char *dummy));
void del_word_left	__P ((char *dummy));
void upcase_word	__P ((char *dummy));
void downcase_word	__P ((char *dummy));
void prev_line		__P ((char *dummy));
void next_line		__P ((char *dummy));
void prev_char		__P ((char *dummy));
void next_char		__P ((char *dummy));
void key_run_command	__P ((char *cmd));

#endif /* _EDIT_H_ */
