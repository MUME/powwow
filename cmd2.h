/* public things from cmd2.c */

void show_aliases	__P ((void));
void parse_alias	__P ((char *str));

void show_actions	__P ((void));
void show_prompts	__P ((void));
void parse_action	__P ((char *str, int onprompt));

void show_attr_syntax	__P ((void));
void attr_string	__P ((int attrcode, char *begin, char *end));
int  parse_attributes	__P ((char *line));
char *attr_name		__P ((int attrcode));
void show_marks		__P ((void));
void parse_mark		__P ((char *str));

char *seq_name		__P ((char *seq, int len));
void show_binds		__P ((char edit));
void parse_bind		__P ((char *arg));
void parse_rebind	__P ((char *arg));

char *redirect		__P ((char *arg, ptr *pbuf, char *kind, char *name, int also_num, long *start, long *end));

void show_vars		__P ((void));
void show_delaynode	__P ((delaynode *p, int in_or_at));
void show_delays	__P ((void));
void change_delaynode	__P ((delaynode **p, char *command, long millisec));
void new_delaynode	__P ((char *name, char *command, long millisec));

void show_history	__P ((int count));
void exe_history	__P ((int count));

