/* public definitions from list.c */

#ifndef _LIST_H_
#define _LIST_H_

int cmp_vtime		__P ((vtime *t1, vtime *t2));
void add_vtime		__P ((vtime *t1, vtime *t2));
long diff_vtime		__P ((vtime *t1, vtime *t2));

int rev_sort		__P ((defnode *node1, defnode *node2));
int ascii_sort		__P ((defnode *node1, defnode *node2));
int rev_ascii_sort	__P ((defnode *node1, defnode *node2));
int time_sort		__P ((defnode *node1, defnode *node2));
int rev_time_sort	__P ((defnode *node1, defnode *node2));

int hash		__P ((char *name));

void add_node		__P ((defnode *newnode, defnode **base, function_sort sort));
void reverse_sortedlist __P ((sortednode **base));

void add_aliasnode	__P ((char *name, char *subst));
void add_actionnode	__P ((char *pattern, char *command, char *label, int active, int type, void *qregexp));
void add_promptnode	__P ((char *pattern, char *command, char *label, int active, int type, void *qregexp));
void add_marknode	__P ((char *pattern, int attrcode, char mbeg, char wild));
void add_keynode	__P ((char *name, char *sequence, int seqlen, function_str funct, char *call_data));
delaynode *add_delaynode   __P ((char *name, char *command, vtime *when, int is_dead));
varnode *add_varnode	__P ((char *name, int type));

aliasnode  **lookup_alias	__P ((char *name));
actionnode **lookup_action	__P ((char *label));
actionnode **lookup_prompt	__P ((char *label));
actionnode **lookup_action_pattern __P ((char *pattern));
marknode   **lookup_marker	__P ((char *pattern, char mbeg));
keynode    **lookup_key		__P ((char *name));
delaynode  **lookup_delay	__P ((char *name, int is_dead));
varnode    **lookup_varnode	__P ((char *name, int type));

void delete_aliasnode	__P ((aliasnode **base));
void delete_actionnode	__P ((actionnode **base));
void delete_promptnode	__P ((promptnode **base));
void delete_marknode	__P ((marknode **base));
void delete_keynode	__P ((keynode **base));
void delete_delaynode	__P ((delaynode **base));
void delete_varnode	__P ((varnode **base, int type));

#endif /* _LIST_H_ */

