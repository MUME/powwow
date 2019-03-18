/* public things from cmd2.c */

void show_aliases(void);
void parse_alias(char *str);

void show_actions(void);
void show_prompts(void);
void parse_action(char *str, int onprompt);

void show_attr_syntax(void);
void attr_string(int attrcode, char *begin, char *end);
int  parse_attributes(char *line);
char *attr_name(int attrcode);
void show_marks(void);
void parse_mark(char *str);

char *seq_name(char *seq, int len);
void show_binds(char edit);
void parse_bind(char *arg);
void parse_rebind(char *arg);

char *redirect(char *arg, ptr *pbuf, char *kind, char *name, int also_num, long *start, long *end);

void show_vars(void);
void show_delaynode(delaynode *p, int in_or_at);
void show_delays(void);
void change_delaynode(delaynode **p, char *command, long millisec);
void new_delaynode(char *name, char *command, long millisec);

void show_history(int count);
void exe_history(int count);

