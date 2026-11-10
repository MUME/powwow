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

#include <stdint.h>
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
#include "list.h"
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

/* the 32-bit FNV-1a hash of 'name' */
static uint32_t hash(const char *name)
{
    uint32_t h = 0x811c9dc5;
    for (const unsigned char *p = (const unsigned char *)name; *p != '\0';
         ++p) {
        h ^= *p;
        h *= 0x01000193;
    }
    return h;
}

/*
 * the list for 'name' in a hash table of MAX_HASH lists: its hash
 * xor-folded to HASH_BITS bits, as the authors of FNV recommend
 */
static uint32_t hash_bucket(const char *name)
{
    uint32_t h = hash(name);
    return ((h >> HASH_BITS) ^ h) & (MAX_HASH - 1);
}

/*
 * insert 'node' into the delays, which are in the order of their time, or
 * if 'dead', into the dead delays, which are in reverse order
 */
void insert_delaynode(delaynode *node, bool dead)
{
    delaynode **base = dead ? &dead_delays : &delays;
    while (*base != NULL
           && (dead
               ? cmp_vtime(&(*base)->when, &node->when) > 0
               : cmp_vtime(&node->when, &(*base)->when) > 0))
        base = &(*base)->next;
    node->next = *base;
    *base = node;
}

/*
 * add a node to the alias list
 */
void add_aliasnode(char *name, char *subst)
{
    aliasnode *new = (aliasnode*)malloc(sizeof(aliasnode));
    if (!new) {
	out_of_memory();
	return;
    }

    new->group = NULL;
    new->active = true;
    new->name = my_strdup(name);
    new->subst = my_strdup(subst);
    if (new->name == NULL || (subst != NULL && new->subst == NULL)) {
	out_of_memory();
	if (new->name)
	    free(new->name);
	if (new->subst)
	    free(new->subst);
	free(new);
	return;
    }
    aliasnode **bucket = &aliases[hash_bucket(name)];
    new->next = *bucket;
    *bucket = new;
    /* the list of the aliases in ASCII order */
    aliasnode **sorted = &sortedaliases;
    while (*sorted != NULL && strcmp((*sorted)->name, new->name) < 0)
        sorted = &(*sorted)->snext;
    new->snext = *sorted;
    *sorted = new;
}

/*
 * add a node to the marker list
 */
void add_marknode(char *pattern, const struct attr *attr, bool mbeg, bool wild)
{
    marknode **p, *new = (marknode*)malloc(sizeof(marknode));
    int i;
    if (!new) {
	out_of_memory();
	return;
    }
    new->next = NULL;
    new->b.pattern = my_strdup(pattern);
    new->b.start = new->b.end = NULL;
    new->b.mbeg = mbeg;
    new->b.wild = wild;
    new->attr = *attr;
    if (new->b.pattern == NULL) {
	out_of_memory();
	free(new);
	return;
    }
    for (p=&markers, i=1; *p && (a_nice==0 || i<a_nice); p = &(*p)->next, i++)
	;
    new->next = *p;
    *p = new;
}


/*
 * add a node to the substitution list
 */
void add_substnode(char *pattern, char *replacement, bool mbeg, bool wild)
{
    substnode *new = malloc(sizeof(*new));
    if (new == NULL) {
        out_of_memory();
        return;
    }
    *new = (substnode){
        .b = { .pattern = my_strdup(pattern), .mbeg = mbeg, .wild = wild },
        .replacement = my_strdup(replacement)
    };
    if (new->b.pattern == NULL || new->replacement == NULL) {
        out_of_memory();
        delete_substnode(&new);
        return;
    }
    substnode **p = &substitutions;
    for (int i = 1; *p != NULL && (a_nice == 0 || i < a_nice); ++i)
      p = &(*p)->next;
    new->next = *p;
    *p = new;
}

/*
 * add a node to the action list
 */
void add_actionnode(char *pattern, char *command, char *label, bool active, int type, void *vregexp)
{
    actionnode **p, *new = (actionnode*)malloc(sizeof(actionnode));
    int i;
    if (!new) {
	out_of_memory();
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
	out_of_memory();
	if (new->pattern)
	    free(new->pattern);
	if (new->command)
	    free(new->command);
	if (new->label)
	    free(new->label);
	free(new);
	return;
    }
    for (p=&actions, i=1; *p && (a_nice==0 || i<a_nice); p = &(*p)->next, i++)
	;
    new->next = *p;
    *p = new;
}

/*
 * add a node to the prompt list
 */
void add_promptnode(char *pattern, char *command, char *label, bool active, int type, void *vregexp)
{
    promptnode **p, *new = (promptnode*)malloc(sizeof(promptnode));
    int i;
    if (!new) {
	out_of_memory();
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
	out_of_memory();
	if (new->pattern)
	    free(new->pattern);
	if (new->command)
	    free(new->command);
	if (new->label)
	    free(new->label);
	free(new);
	return;
    }
    for (p=&prompts, i=1; *p && (a_nice==0 || i<a_nice); p = &(*p)->next, i++)
	;
    new->next = *p;
    *p = new;
}

/*
 * add a node to the keydef list
 */
void add_keynode(const char *name, const char *sequence, int seqlen,
                 function_str funct, const char *call_data)
{
    keynode *new = (keynode*)malloc(sizeof(keynode));
    if (!new) {
	out_of_memory();
	return;
    }
    new->name = my_strdup(name);
    if (!seqlen) seqlen = strlen(sequence);
    new->sequence = malloc(seqlen + 1);
    if (new->sequence != NULL)
        memmove(new->sequence, sequence, seqlen);
    new->seqlen = seqlen;
    new->funct = funct;
    new->call_data = my_strdup(call_data);
    if (!new->name || !new->sequence || (call_data && !new->call_data)) {
	out_of_memory();
	if (new->name)
	    free(new->name);
	if (new->sequence)
	    free(new->sequence);
	if (new->call_data)
	    free(new->call_data);
	free(new);
	return;
    }
    /* in ASCII order of their names */
    keynode **base = &keydefs;
    while (*base != NULL && strcmp(new->name, (*base)->name) > 0)
        base = &(*base)->next;
    new->next = *base;
    *base = new;
}

/*
 * add a node to the delayed command list
 * is_dead == 1 means when < now (and so cannot be executed anymore)
 */
delaynode *add_delaynode(char *name, char *command, vtime *when, int is_dead)
{
    delaynode *new = (delaynode*)malloc(sizeof(delaynode));
    if (!new) {
	out_of_memory();
	return NULL;
    }
    new->name = my_strdup(name);
    new->command = my_strdup(command);
    if (!new->name || (command && !new->command)) {
	out_of_memory();
	if (new->name)
	    free(new->name);
	if (new->command)
	    free(new->command);
	free(new);
	return NULL;
    }

    new->when.tv_sec = when->tv_sec;
    new->when.tv_usec = when->tv_usec;
    insert_delaynode(new, is_dead != 0);

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
		out_of_memory();
		return NULL;
	    }
	    var = newvar;
	    max_named_vars += n-m;
	    memset(var + m, 0, (n - m) * sizeof(vars));
	}
    }

    new = (varnode*)malloc(sizeof(varnode));
    if (!new) {
	out_of_memory();
	return NULL;
    }
    new->name = my_strdup(name);
    if (new->name == NULL) {
        out_of_memory();
        free(new);
        return NULL;
    }
    new->num = 0;
    new->str = (ptr *)0;
    new->index = m = NUMPARAM + num_named_vars[type];

    if (type)
	VAR[m].str = &new->str;
    else
	VAR[m].num = &new->num;
    num_named_vars[type]++;

    varnode **bucket = &named_vars[type][hash_bucket(name)];
    new->next = *bucket;
    *bucket = new;
    /* the list of the variables in ASCII order */
    varnode **sorted = &sortednamed_vars[type];
    while (*sorted != NULL && strcmp((*sorted)->name, new->name) < 0)
        sorted = &(*sorted)->snext;
    new->snext = *sorted;
    *sorted = new;
    return new;
}

/*
 * look up an alias node by name:
 * return pointer to pointer to node or a pointer to NULL if nothing found
 */
aliasnode **lookup_alias(char *name)
{
    aliasnode **p = &aliases[hash_bucket(name)];
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
 * look up a marker node by pattern:
 * return pointer to pointer to node or a pointer to NULL if nothing found
 */
marknode **lookup_marker(char *pattern, bool mbeg)
{
    marknode **p = &markers;
    while (*p && (mbeg != (*p)->b.mbeg || strcmp(pattern, (*p)->b.pattern)))
	p = &(*p)->next;
    return p;
}

/*
 * look up a substitution node by pattern:
 * return pointer to pointer to node or a pointer to NULL if nothing found
 */
substnode **lookup_subst(char *pattern, bool mbeg)
{
    substnode **p = &substitutions;
    while (*p != NULL
           && (mbeg != (*p)->b.mbeg || strcmp(pattern, (*p)->b.pattern) != 0))
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
    varnode **p = &named_vars[type][hash_bucket(name)];
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
    aliasnode **sorted = &sortedaliases;
    while (*sorted != NULL && *sorted != p)
        sorted = &(*sorted)->snext;
    if (*sorted == NULL) {
        wrap_printf("#internal error, alias \"%s\" is not in the sorted list",
                    p->name);
        error = INTERNAL_ERROR;
        return;
    }
    *sorted = p->snext;
    if (p->name) free(p->name);
    if (p->subst) free(p->subst);
    free(p->group);
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
    free(p->group);
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
    free(p->group);
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
    if (p->b.pattern != NULL) free(p->b.pattern);
    *base = p->next;
    free((void*)p);
}

/*
 * delete a substnode, given a pointer to its precessor's pointer
 */
void delete_substnode(substnode **base)
{
    substnode *p = *base;
    free(p->b.pattern);
    free(p->replacement);
    *base = p->next;
    free(p);
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
    varnode **sorted = &sortednamed_vars[type];
    while (*sorted != NULL && *sorted != p)
        sorted = &(*sorted)->snext;
    if (*sorted == NULL) {
        wrap_printf("#internal error, variable \"%s\" is not in the sorted"
                    " list", p->name);
        error = INTERNAL_ERROR;
        return;
    }
    *sorted = p->snext;
    if (p->name) free(p->name);
    if (type != 0 && p->str != NULL) ptrdel(&p->str);
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
