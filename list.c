/*
 *  list.c  --  list utility functions.
 *
 *  Copyright (C) 1998 by Massimiliano Ghilardi
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/types.h>
#include <sys/time.h>

#include "defines.h"
#include "main.h"
#include "feature/regex.h"
#include "utils.h"
#include "cmd2.h"
#include "tty.h"
#include "eval.h"

/*
 * compare two times, return -1 if t1 < t2, 1 if t1 > t2, 0 if t1 == t2
 */
int cmp_vtime(vtime *t1, vtime *t2)
{
    int i;
    i = t1->tv_sec < t2->tv_sec ? -1 : t1->tv_sec > t2->tv_sec ? 1 : 0;
    if (!i)
	i = t1->tv_usec < t2->tv_usec ? -1 : t1->tv_usec > t2->tv_usec ? 1 : 0;
    return i;
}

/*
 * add t2 to t1 (i.e. t1 += t2)
 */
void add_vtime(vtime *t1, vtime *t2)
{
    t1->tv_sec += t2->tv_sec;
    if ((t1->tv_usec += t2->tv_usec) >= uSEC_PER_SEC) {
	t1->tv_sec += t1->tv_usec / uSEC_PER_SEC;
	t1->tv_usec %= uSEC_PER_SEC;
    }
}

/*
 * Return t1 - t2, in milliseconds
 */
long diff_vtime(vtime *t1, vtime *t2)
{
    return (t1->tv_sec - t2->tv_sec) * mSEC_PER_SEC +
	(t1->tv_usec - t2->tv_usec) / uSEC_PER_mSEC;
}

int rev_sort(defnode *node1, defnode *node2)
{
    return -1;
}

/*
 * standard ASCII comparison between nodes
 */
int ascii_sort(defnode *node1, defnode *node2)
{
    return strcmp(node1->sortfield, node2->sortfield);
}

int rev_ascii_sort(defnode *node1, defnode *node2)
{
    return strcmp(node2->sortfield, node1->sortfield);
}


/*
 * comparison between times of execution of nodes
 * (return -1 if node1->when < node2->when)
 */
int time_sort(defnode *node1, defnode *node2)
{
    return cmp_vtime(&((delaynode *)node1)->when, &((delaynode *)node2)->when);
}

/*
 * reverse comparison between times of execution of nodes
 * (return -1 if node1->when > node2->when)
 */
int rev_time_sort(defnode *node1, defnode *node2)
{
    return cmp_vtime(&((delaynode *)node2)->when, &((delaynode *)node1)->when);
}

/*
 * compute the hash value of a name
 */
int hash(char *name, int optlen)
{
    int h = 0, i = 0;
    if (optlen < 0)
	optlen = strlen(name);
    while (optlen-- > 0) {
	h += ((*name++) ^ i) << i;
	if (++i == LOG_MAX_HASH)
	    i = 0;
    }
    return (h + (h >> LOG_MAX_HASH) + (h >> (2*LOG_MAX_HASH))) & (MAX_HASH-1);
}

/*
 * generic list node adding routine
 */
void add_node(defnode *newnode, defnode **base, function_sort sort)
{
    while((*base) && (!sort || (*sort)(newnode, *base) > 0))
	base = &(*base)->next;
    newnode->next = *base;
    *base = newnode;
}

static void add_sortednode(sortednode *newnode, sortednode **base, function_sort sort)
{
    while((*base) && (!sort || (*sort)((defnode *)newnode, (defnode *)*base) > 0))
	base = &(*base)->snext;
    newnode->snext = *base;
    *base = newnode;
}

void reverse_list(defnode **base)
{
    defnode *node = *base, *list = NULL, *tmp;
    while (node) {
	tmp = node->next;
	node->next = list;
	list = node;
	node = tmp;
    }
    *base = list;
}

void reverse_sortedlist(sortednode **base)
{
    sortednode *node = *base, *list = NULL, *tmp;
    while (node) {
	tmp = node->snext;
	node->snext = list;
	list = node;
	node = tmp;
    }
    *base = list;
}

static sortednode **selflookup_sortednode(sortednode *self, sortednode **base)
{
    sortednode **p = base;
    while (*p && *p != self)
	p = &(*p)->snext;
    if (!*p) {
	PRINTF("#internal error, selflookup_sortednode(\"%s\") failed!\n", self->sortfield);
	error = INTERNAL_ERROR;
    }
    return p;
}

/*
 * add a node to the alias list
 */
void add_aliasnode(char *name, char *subst)
{
    aliasnode *new = (aliasnode*)malloc(sizeof(aliasnode));
    if (!new) {
	errmsg("malloc");
	return;
    }

    new->group = NULL;
    new->active = 1;
    new->name = my_strdup(name);
    new->subst = my_strdup(subst);
    if ((name && !new->name) || (subst && !new->subst)) {
	errmsg("malloc");
	if (new->name)
	    free(new->name);
	if (new->subst)
	    free(new->subst);
	free(new);
	return;
    }
    add_node((defnode*)new, (defnode**)&aliases[hash(name,-1)], rev_sort);
    add_sortednode((sortednode*)new, (sortednode**)&sortedaliases, rev_ascii_sort);
}

/*
 * add a node to the marker list
 */
void add_marknode(char *pattern, int attrcode, char mbeg, char wild)
{
    marknode **p, *new = (marknode*)malloc(sizeof(marknode));
    int i;
    if (!new) {
	errmsg("malloc");
	return;
    }
    new->pattern = my_strdup(pattern);
    new->attrcode = attrcode;
    new->start = new->end = NULL;
    new->mbeg = mbeg;
    new->wild = wild;
    if (!new->pattern) {
	errmsg("malloc");
	free(new);
	return;
    }
#ifdef DO_SORT
    add_node((defnode*)new, (defnode**)&markers, ascii_sort);
#else
    for (p=&markers, i=1; *p && (a_nice==0 || i<a_nice); p = &(*p)->next, i++)
	;
    new->next = *p;
    *p = new;
#endif
}

/*
 * add a node to the action list
 */
void add_actionnode(char *pattern, char *command, char *label, int active, int type, void *vregexp)
{
    actionnode **p, *new = (actionnode*)malloc(sizeof(actionnode));
    int i;
    if (!new) {
	errmsg("malloc");
	return;
    }

    new->group = NULL;
    new->pattern = my_strdup(pattern);
    new->command = my_strdup(command);
    new->label = my_strdup(label);
    new->active = active;
    new->type = type;
#ifdef USE_REGEXP
    new->regexp = vregexp;
#endif
    if (!new->pattern || (command && !new->command) || (label && !new->label)) {
	errmsg("malloc");
	if (new->pattern)
	    free(new->pattern);
	if (new->command)
	    free(new->command);
	if (new->label)
	    free(new->label);
	free(new);
	return;
    }
#ifdef DO_SORT
    add_node((defnode*)new, (defnode**)&actions, ascii_sort);
#else
    for (p=&actions, i=1; *p && (a_nice==0 || i<a_nice); p = &(*p)->next, i++)
	;
    new->next = *p;
    *p = new;
#endif
}

/*
 * add a node to the prompt list
 */
void add_promptnode(char *pattern, char *command, char *label, int active, int type, void *vregexp)
{
    promptnode **p, *new = (promptnode*)malloc(sizeof(promptnode));
    int i;
    if (!new) {
	errmsg("malloc");
	return;
    }

    new->pattern = my_strdup(pattern);
    new->command = my_strdup(command);
    new->label = my_strdup(label);
    new->active = active;
    new->type = type;
#ifdef USE_REGEXP
    new->regexp = vregexp;
#endif
    if (!new->pattern || (command && !new->command) || (label && !new->label)) {
	errmsg("malloc");
	if (new->pattern)
	    free(new->pattern);
	if (new->command)
	    free(new->command);
	if (new->label)
	    free(new->label);
	free(new);
	return;
    }
#ifdef DO_SORT
    add_node((defnode*)new, (defnode**)&prompts, ascii_sort);
#else
    for (p=&prompts, i=1; *p && (a_nice==0 || i<a_nice); p = &(*p)->next, i++)
	;
    new->next = *p;
    *p = new;
#endif
}

/*
 * add a node to the keydef list
 */
void add_keynode(char *name, char *sequence, int seqlen, function_str funct, char *call_data)
{
    keynode *new = (keynode*)malloc(sizeof(keynode));
    if (!new) {
	errmsg("malloc");
	return;
    }
    new->name = my_strdup(name);
    if (!seqlen) seqlen = strlen(sequence);
    new->sequence = (char *)malloc(seqlen + 1);
    memmove(new->sequence, sequence, seqlen);
    new->seqlen = seqlen;
    new->funct = funct;
    new->call_data = my_strdup(call_data);
    if (!new->name || !new->sequence || (call_data && !new->call_data)) {
	errmsg("malloc");
	if (new->name)
	    free(new->name);
	if (new->sequence)
	    free(new->sequence);
	if (new->call_data)
	    free(new->call_data);
	free(new);
	return;
    }
    add_node((defnode*)new, (defnode**)&keydefs, ascii_sort);
}

/*
 * add a node to the delayed command list
 * is_dead == 1 means when < now (and so cannot be executed anymore)
 */
delaynode *add_delaynode(char *name, char *command, vtime *when, int is_dead)
{
    delaynode *new = (delaynode*)malloc(sizeof(delaynode));
    if (!new) {
	errmsg("malloc");
	return NULL;
    }
    new->name = my_strdup(name);
    new->command = my_strdup(command);
    if (!new->name || (command && !new->command)) {
	errmsg("malloc");
	if (new->name)
	    free(new->name);
	if (new->command)
	    free(new->command);
	free(new);
	return NULL;
    }

    new->when.tv_sec = when->tv_sec;
    new->when.tv_usec = when->tv_usec;
    if (is_dead)
	add_node((defnode*)new, (defnode**)&dead_delays, rev_time_sort);
    else
	add_node((defnode*)new, (defnode**)&delays, time_sort);

    return new;
}

/*
 * add a node to named variables list
 *
 * do NOT allocate a ptr!
 */
varnode *add_varnode(char *name, int type)
{
    varnode *new;
    int m, n;

    if (type)
	type = 1;

    if (num_named_vars[type] >= max_named_vars) {
	/* we are running low on var pointers. try to enlarge */
	m = NUMTOT + max_named_vars;
	n = NUMTOT + max_named_vars * 2;
	if (n < 0) {
	    /* overflow */
	    print_error(error=OUT_OF_VAR_SPACE_ERROR);
	    return NULL;
	}
	else {
	    vars *newvar;
	    if ((newvar = (vars *)realloc(var, n*sizeof(vars) )))
		;
	    else if ((newvar = (vars *)malloc( n*sizeof(vars) ))) {
	        memmove(newvar, var, m * sizeof(vars));
		free((void *)var);
	    } else {
		errmsg("malloc");
		return NULL;
	    }
	    var = newvar;
	    max_named_vars += n-m;
	    memzero(var + m, (n-m)*sizeof(vars));
	}
    }

    new = (varnode*)malloc(sizeof(varnode));
    if (!new) {
	errmsg("malloc");
	return NULL;
    }
    new->name = my_strdup(name);
    if (name && !new->name) {
	errmsg("malloc");
	free(new);
	return NULL;
    }
    new->num = 0;
    new->str = (ptr)0;
    new->index = m = NUMPARAM + num_named_vars[type];

    if (type)
	VAR[m].str = &new->str;
    else
	VAR[m].num = &new->num;
    num_named_vars[type]++;

    add_node((defnode*)new, (defnode**)&named_vars[type][hash(name,-1)], rev_sort);
    add_sortednode((sortednode*)new, (sortednode**)&sortednamed_vars[type], rev_ascii_sort);
    return new;
}

/*
 * look up an alias node by name:
 * return pointer to pointer to node or a pointer to NULL if nothing found
 */
aliasnode **lookup_alias(char *name)
{
    aliasnode **p = &aliases[hash(name,-1)];
    while (*p && strcmp(name, (*p)->name))
	p = &(*p)->next;
    return p;
}

/*
 * look up an action node by label:
 * return pointer to pointer to node or a pointer to NULL if nothing found
 */
actionnode **lookup_action(char *label)
{
    actionnode **p = &actions;
    while (*p && strcmp(label, (*p)->label))
	p = &(*p)->next;
    return p;
}

/*
 * look up an action node by pattern:
 * return pointer to pointer to node or a pointer to NULL if nothing found
 */
actionnode **lookup_action_pattern(char *pattern)
{
    actionnode **p = &actions;
    while (*p && strcmp(pattern, (*p)->pattern))
	p = &(*p)->next;
    return p;
}

/*
 * look up a prompt node by label:
 * return pointer to pointer to node or a pointer to NULL if nothing found
 */
actionnode **lookup_prompt(char *label)
{
    promptnode **p = &prompts;
    while (*p && strcmp(label, (*p)->label))
	p = &(*p)->next;
    return p;
}

/*
 * look up an marker node by pattern:
 * return pointer to pointer to node or a pointer to NULL if nothing found
 */
marknode **lookup_marker(char *pattern, char mbeg)
{
    marknode **p = &markers;
    while (*p && (mbeg != (*p)->mbeg || strcmp(pattern, (*p)->pattern)))
	p = &(*p)->next;
    return p;
}

/*
 * look up a key node by name:
 * return pointer to pointer to node or a pointer to NULL if nothing found
 */
keynode **lookup_key(char *name)
{
    keynode **p = &keydefs;

    while (*p && strcmp(name, (*p)->name))
	p = &(*p)->next;
    return p;
}

/*
 * look up a delayed command node by label:
 * return pointer to pointer to node or a pointer to NULL if nothing found
 */
delaynode **lookup_delay(char *name, int is_dead)
{
    delaynode **p = (is_dead ? &dead_delays : &delays);
    while (*p && strcmp(name, (*p)->name))
        p = &(*p)->next;
    return p;
}

/*
 * look up a named variable node by name:
 * return pointer to pointer to node or a pointer to NULL if nothing found
 */
varnode **lookup_varnode(char *name, int type)
{
    varnode **p = &named_vars[type][hash(name,-1)];
    while (*p && strcmp(name, (*p)->name))
	p = &(*p)->next;
    return p;
}

/*
 * delete an alias node, given a pointer to its precessor's pointer
 */
void delete_aliasnode(aliasnode **base)
{
    aliasnode *p = *base;
    *base = p->next;
    if (*(base = (aliasnode**)selflookup_sortednode
	  ((sortednode*)p, (sortednode**)&sortedaliases)))
	*base = p->snext;
    else
	return;
    if (p->name) free(p->name);
    if (p->subst) free(p->subst);
    free((void*)p);
}

/*
 * delete an action node, given a pointer to its precessor's pointer
 */
void delete_actionnode(actionnode **base)
{
    actionnode *p = *base;
    if (p->pattern) free(p->pattern);
    if (p->command) free(p->command);
    if (p->label) free(p->label);
#ifdef USE_REGEXP
    if (p->type == ACTION_REGEXP && p->regexp) {
	regfree((regex_t *)p->regexp);
	free(p->regexp);
    }
#endif
    *base = p->next;
    free((void*)p);
}

/*
 * delete an prompt node, given a pointer to its precessor's pointer
 */
void delete_promptnode(promptnode **base)
{
    promptnode *p = *base;
    if (p->pattern) free(p->pattern);
    if (p->command) free(p->command);
    if (p->label) free(p->label);
#ifdef USE_REGEXP
    if (p->type == ACTION_REGEXP && p->regexp) {
	regfree((regex_t *)p->regexp);
	free(p->regexp);
    }
#endif
    *base = p->next;
    free((void*)p);
}

/*
 * delete an marker node, given a pointer to its precessor's pointer
 */
void delete_marknode(marknode **base)
{
    marknode *p = *base;
    if (p->pattern) free(p->pattern);
    *base = p->next;
    free((void*)p);
}

/*
 * delete a keydef node, given a pointer to its precessor's pointer
 */
void delete_keynode(keynode **base)
{
    keynode *p = *base;
    if (p->name) free(p->name);
    if (p->sequence) free(p->sequence);
    if (p->call_data) free(p->call_data);
    *base = p->next;
    free((void*)p);
}

/*
 * delete a delayed command node, given a pointer to its precessor's pointer
 */
void delete_delaynode(delaynode **base)
{
    delaynode *p = *base;
    if (p->name) free(p->name);
    if (p->command) free(p->command);
    *base = p->next;
    free((void*)p);
}

/*
 * delete a named variable node, given a pointer to its precessor's pointer
 */
void delete_varnode(varnode **base, int type)
{
    varnode *p = *base;
    int idx = p->index, i, n;

    *base = p->next;
    if (*(base = (varnode**)selflookup_sortednode
	  ((sortednode*)p, (sortednode**)&sortednamed_vars[type])))
	*base = p->snext;
    else
	return;
    if (p->name) free(p->name);
    if (type && p->str) ptrdel(p->str);
    free((void*)p);

    i = NUMPARAM + --num_named_vars[type];

    if (idx == i)
	return;

    /* now I must fill the hole in var[idx].*** */

    for (n = 0; n < MAX_HASH; n++)
	for (p = named_vars[type][n]; p; p = p->next)
	if (p->index == i) {
	    n = MAX_HASH;
	    break;
	}

    if (!p) {               /* should NEVER happen */
	print_error(error=UNDEFINED_VARIABLE_ERROR);
	return;
    }

    p->index = idx;

    if (type) {
	VAR[idx].str = &p->str;
	VAR[ i ].str = NULL;
    } else {
	VAR[idx].num = &p->num;
	VAR[ i ].num = NULL;
    }
}
