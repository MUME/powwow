/* public definitions from list.c */

#ifndef _LIST_H_
#define _LIST_H_

int cmp_vtime(vtime *t1, vtime *t2);
void add_vtime(vtime *t1, vtime *t2);
long diff_vtime(vtime *t1, vtime *t2);



void insert_delaynode(delaynode *node, bool dead);

void add_aliasnode(char *name, char *subst);
void add_actionnode(char *pattern, char *command, char *label, bool active, int type, void *vregexp);
void add_promptnode(char *pattern, char *command, char *label, bool active, int type, void *vregexp);
void add_marknode(char *pattern, const struct attr *attr, bool mbeg, bool wild);
void add_keynode(const char *name, const char *sequence, int seqlen,
                 function_str funct, const char *call_data);
void add_substnode(char *pattern, char *replacement, bool mbeg, bool wild);
delaynode *add_delaynode(char *name, char *command, vtime *when, int is_dead);
varnode *add_varnode(const char *name, enum var_kind kind);

aliasnode  **lookup_alias(char *name);
actionnode **lookup_action(char *label);
actionnode **lookup_prompt(char *label);
actionnode **lookup_action_pattern(char *pattern);
marknode   **lookup_marker(char *pattern, bool mbeg);
substnode  **lookup_subst(char *pattern, bool mbeg);
keynode    **lookup_key(const char *name);
delaynode  **lookup_delay(char *name, int is_dead);
varnode    **lookup_varnode(char *name, enum var_kind kind);

void delete_aliasnode(aliasnode **base);
void delete_actionnode(actionnode **base);
void delete_promptnode(promptnode **base);
void delete_marknode(marknode **base);
void delete_keynode(keynode **base);
void delete_delaynode(delaynode **base);
void delete_substnode(substnode **base);
void delete_varnode(varnode **base, enum var_kind kind);

#endif /* _LIST_H_ */

