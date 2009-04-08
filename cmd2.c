/*
 *  cmd2.c  --  back-end for the various #commands
 *
 *  (created: Massimiliano Ghilardi (Cosmos), Aug 14th, 1998)
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
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <ctype.h>
#include <time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include <errno.h>

#ifdef USE_REGEXP
# include <regex.h>
#endif

int strcasecmp();
int select();

#include "defines.h"
#include "main.h"
#include "utils.h"
#include "beam.h"
#include "edit.h"
#include "list.h"
#include "map.h"
#include "tcp.h"
#include "tty.h"
#include "eval.h"

/* anyone knows if ANSI 6429 talks about more than 8 colors? */
static char *colornames[] = {
    "black", "red", "green", "yellow", "blue", "magenta", "cyan", "white",
    "BLACK", "RED", "GREEN", "YELLOW", "BLUE", "MAGENTA", "CYAN", "WHITE", "none"
};

/*
 * show defined aliases
 */
void show_aliases __P0 (void)
{
    aliasnode *p;
    char buf[BUFSIZE];
    
    PRINTF("#%s alias%s defined%c\n", sortedaliases ? "the following" : "no",
	       (sortedaliases && !sortedaliases->snext) ? " is" : "es are",
	       sortedaliases ? ':' : '.');
    reverse_sortedlist((sortednode **)&sortedaliases);
    for (p = sortedaliases; p; p = p->snext) {
	escape_specials(buf, p->name);
	tty_printf("#alias %s%s%s%s=%s\n", 
			p->active ? "" : "(disabled) ",
			buf, group_delim, p->group == NULL ? "*" : p->group, p->subst);
    }
    reverse_sortedlist((sortednode **)&sortedaliases);
}

/*
 * check if an alias name contains dangerous things.
 * return 1 if illegal name (and print reason).
 * if valid, print a warning for unbalanced () {} and ""
 */
static int check_alias __P1 (char *,name)
{
    char *p = name, c;
    enum { NORM, ESCAPE } state = NORM;
    int quotes=0, paren=0, braces=0, ok = 1;

    if (!*p) {
	PRINTF("#illegal alias name: is empty!\n");
	error = INVALID_NAME_ERROR;
	return 1;
    }
    if (*p == '{') {
	PRINTF("#illegal beginning '{' in alias name: \"%s\"\n", name);
	error = INVALID_NAME_ERROR;
	return 1;
    }
    if (strchr(name, ' ')) {
	PRINTF("#illegal spaces in alias name: \"%s\"\n", name);
	error = INVALID_NAME_ERROR;
	return 1;
    }

    for (; ok && (c = *p); p++) switch (state) {
      case NORM:
	if (c == ESC)
	    state = ESCAPE;
	else if (quotes) {
	    if (c == '\"')
		quotes = 0;
	}
	else if (c == '\"')
	    quotes = 1;
	else if (c == ')')
	    paren--;
	else if (c == '(')
	    paren++;
	else if (c == '}')
	    braces--;
	else if (c == '{')
	    braces++;
	else if (c == CMDSEP && !paren && !braces)
	    ok = 0;
	break;
      case ESCAPE:
	if (c == ESC)
	    state = ESCAPE;
	else /* if (c == ESC2 || c != ESC2) */
	    state = NORM;
      default:
	break;
    }
	
    if (!ok) {
	PRINTF("#illegal non-escaped ';' in alias name: \"%s\"\n", name);
	error = INVALID_NAME_ERROR;
	return 1;
    }
	
    if (quotes || paren || braces) {
	PRINTF("#warning: unbalanced%s%s%s in alias name \"%s\" may cause problems\n",
	       quotes ? " \"\"" : "", paren ? " ()" : "", braces ? " {}" : "", name);
    }

    return 0;
}


/*
 * parse the #alias command
 */
void parse_alias __P1 (char *,str)
{
    char *left, *right, *group;
    aliasnode **np, *p;
    
    left = str = skipspace(str);
    
    str = first_valid(str, '=');

    if (*str == '=') {
        *str = '\0';
        right = ++str;
        unescape(left);

    	/* break out group name (if present) */
        group = strstr( left, group_delim );
	if( group ) {
	    *group = 0;
	    group += strlen( group_delim );
	}

	if (check_alias(left))
	    return;
        p = *(np = lookup_alias(left));
        if (!*str) {
            /* delete alias */
            if (p) {
                if (opt_info) {
                    PRINTF("#deleting alias: %s=%s\n", left, p->subst);
                }
                delete_aliasnode(np);
            } else {
                PRINTF("#unknown alias, cannot delete: \"%s\"\n", left);
            }
        } else {
            /* add/redefine alias */
	    
	    /* direct recursion is supported (alias CAN be defined by itself) */
            if (p) {
                free(p->subst);
                p->subst = my_strdup(right);
            } else 
                add_aliasnode(left, right);

	    /* get alias again to add group (if needed)
	     * don't take the lookup penalty though if not changing groups */
	    if( group != NULL ) {
                np = lookup_alias(left);
		if( (*np)->group != NULL )
			free((*np)->group);

		if ( *group == '\0' || strcmp(group,"*") == 0 )
			group = NULL;

	    	(*np)->group = my_strdup(group);
	    }

            if (opt_info) {
                PRINTF("#%s alias in group '%s': %s=%s\n", p ? "changed" : "new",
			group == NULL ? "*" : group, left, right);
            }
        }
    } else {
        /* show alias */
	
        *str = '\0';
        unescape(left);
	if (check_alias(left))
	    return;
        np = lookup_alias(left);
        if (*np) {
	    char buf[BUFSIZE];
            escape_specials(buf, left);
            snprintf(inserted_next, BUFSIZE, "#alias %s%s%s=%s",
                buf, 
                group_delim,
                (*np)->group == NULL ? "*" : (*np)->group,
		        (*np)->subst);
        } else {
            PRINTF("#unknown alias, cannot show: \"%s\"\n", left);
        }
    }
}

/*
 * delete an action node
 */
static void delete_action __P1 (actionnode **,nodep)
{
    if (opt_info) {
        PRINTF("#deleting action: >%c%s %s\n", (*nodep)->active ?
		   '+' : '-', (*nodep)->label, (*nodep)->pattern);
    }
    delete_actionnode(nodep);
}

/*
 * delete a prompt node
 */
static void delete_prompt __P1 (actionnode **,nodep)
{
    if (opt_info) {
        PRINTF("#deleting prompt: >%c%s %s\n", (*nodep)->active ?
		   '+' : '-', (*nodep)->label, (*nodep)->pattern);
    }
    delete_promptnode(nodep);
}

/*
 * create new action
 */
static void add_new_action __P6 (char *,label, char *,pattern, char *,command, int,active, int,type, void *,q)
{
    add_actionnode(pattern, command, label, active, type, q);
    if (opt_info) {
        PRINTF("#new action: %c%c%s %s=%s\n", 
		   action_chars[type],
		   active ? '+' : '-', label,
		   pattern, command);
    }
}

/*
 * create new prompt
 */
static void add_new_prompt __P6 (char *,label, char *,pattern, char *,command, int,active, int,type, void *,q)
{
    add_promptnode(pattern, command, label, active, type, q);
    if (opt_info) {
        PRINTF("#new prompt: %c%c%s %s=%s\n", 
		   action_chars[type],
		   active ? '+' : '-', label,
		   pattern, command);
    }
}

/*
 * add an action with numbered label
 */
static void add_anonymous_action __P4 (char *,pattern, char *,command, int,type, void *,q)
{
    static int last = 0;
    char label[16];
    do {
        sprintf(label, "%d", ++last);
    } while (*lookup_action(label));
    add_new_action(label, pattern, command, 1, type, q);
}

#define ONPROMPT (onprompt ? "prompt" : "action")

/*
 * change fields of an existing action node
 * pattern or commands can be NULL if no change
 */
static void change_actionorprompt __P6 (actionnode *,node, char *,pattern, char *,command, int,type, void *,q, int,onprompt)
{
#ifdef USE_REGEXP
    if (node->type == ACTION_REGEXP && node->regexp) {
	regfree((regex_t *)(node->regexp));
	free(node->regexp);
    }
    node->regexp = q;
#endif
    if (pattern) {
        free(node->pattern);
        node->pattern = my_strdup(pattern);
	node->type = type;
    }
    if (command) {
        free(node->command);
        node->command = my_strdup(command);
    }
    
    if (opt_info) {
        PRINTF("#changed %s %c%c%s %s=%s\n", ONPROMPT,
		   action_chars[node->type],
		   node->active ? '+' : '-',
		   node->label, node->pattern, node->command);
    }
}

/*
 * show defined actions
 */
void show_actions __P0 (void)
{
    actionnode *p;
    
    PRINTF("#%s action%s defined%c\n", actions ? "the following" : "no",
	       (actions && !actions->next) ? " is" : "s are", actions ? ':' : '.');
    for (p = actions; p; p = p->next)
	tty_printf("#action %c%c%s%s%s %s=%s\n",
		   action_chars[p->type],
		   p->active ? '+' : '-', p->label,
           group_delim,
		   p->group == NULL ? "*" : p->group, 
		   p->pattern,
		   p->command);
}

/*
 * show defined prompts
 */
void show_prompts __P0 (void)
{
    promptnode *p;
    
    PRINTF("#%s prompt%s defined%c\n", prompts ? "the following" : "no",
	       (prompts && !prompts->next) ? " is" : "s are", prompts ? ':' : '.');
    for (p = prompts; p; p = p->next)
	tty_printf("#prompt %c%c%s %s=%s\n",
		   action_chars[p->type],
		   p->active ? '+' : '-', p->label,
		   p->pattern, p->command);
}

/*
 * parse the #action and #prompt commands
 * this function is too damn complex because of the hairy syntax. it should be
 * split up or rewritten as an fsm instead.
 */
void parse_action __P2 (char *,str, int,onprompt)
{
    char *p, label[BUFSIZE], pattern[BUFSIZE], *command, *group;
    actionnode **np = NULL;
    char sign, assign, hastail;
    char active, type = ACTION_WEAK, kind;
    void *regexp = 0;
    
    sign = *(p = skipspace(str));
    if (!sign) {
	PRINTF("%s: no arguments given\n", ONPROMPT);
	return;
    }
    
    str = p + 1;
    
    switch (sign) {
      case '+':
      case '-':		/* edit */
      case '<':		/* delete */
      case '=':		/* list */
	assign = sign;
	break;
      case '%': /* action_chars[ACTION_REGEXP] */
	type = ACTION_REGEXP;
	/* falltrough */
      case '>': /* action_chars[ACTION_WEAK] */
	
	/* define/edit */
	assign = '>';
	sign = *(p + 1);
	if (!sign) {
	    PRINTF("#%s: label expected\n", ONPROMPT);
	    return;
	} else if (sign == '+' || sign == '-')
	    str++;
	else
	    sign = '+';
	break;
      default:
	assign = 0;	/* anonymous action */
	str = p;
	break;
    }
    
    /* labelled action: */
    if (assign != 0) {
        p = first_regular(str, ' ');
	if ((hastail = *p))
	    *p = '\0';

        /* break out group name (if present) */
	group = strstr( str, group_delim );
	if( group ) {
		*group = 0;
		group += strlen( group_delim );
	}
	
	my_strncpy(label, str, BUFSIZE-1);
	if (hastail)
	    *p++ = ' ';	/* p points to start of pattern, or to \0 */
	
	if (!*label) {
	    PRINTF("#%s: label expected\n", ONPROMPT);
	    return;
	}


	if (onprompt)
	    np = lookup_prompt(label);
	else
	    np = lookup_action(label);
	
	/* '<' : remove action */
        if (assign == '<') {
            if (!np || !*np) {
                PRINTF("#no %s, cannot delete label: \"%s\"\n",
		       ONPROMPT, label);
            }
            else {
		if (onprompt)
		    delete_prompt(np);
		else
		    delete_action(np);
	    }
	    
	    /* '>' : define action */
        } else if (assign == '>') {
#ifndef USE_REGEXP
	    if (type == ACTION_REGEXP) {
		PRINTF("#error: regexp not allowed\n");
		return;
	    }	    
#endif
	    
            if (sign == '+')
		active = 1;
	    else
		active = 0;
	    
	    if (!*label) {
		PRINTF("#%s: label expected\n", ONPROMPT);
		return;
	    }
	    
            p = skipspace(p);
            if (*p == '(') {
		ptr pbuf = (ptr)0;
		p++;
		kind = evalp(&pbuf, &p);
		if (!REAL_ERROR && kind != TYPE_TXT)
		    error=NO_STRING_ERROR;
		if (REAL_ERROR) {
		    PRINTF("#%s: ", ONPROMPT);
		    print_error(error=NO_STRING_ERROR);
		    ptrdel(pbuf);
		    return;
		}
		if (pbuf) {
		    my_strncpy(pattern, ptrdata(pbuf), BUFSIZE-1);
		    ptrdel(pbuf);
		} else
		    pattern[0] = '\0';
		if (*p)
		    p = skipspace(++p);
		if ((hastail = *p == '='))
		    p++;
	    }
            else {
		p = first_regular(command = p, '=');
		if ((hastail = *p))
		    *p = '\0';
		my_strncpy(pattern, command, BUFSIZE-1);
		
		if (hastail)
		    *p++ = '=';
	    }
	    
	    if (!*pattern) {
		PRINTF("#error: pattern of #%ss must be non-empty.\n", ONPROMPT);
		return;
	    }
	    
#ifdef USE_REGEXP
	    if (type == ACTION_REGEXP && hastail) {
		int errcode;
		char unesc_pat[BUFSIZE];
		
		/*
		 * HACK WARNING:
		 * we unescape regexp patterns now, instead of doing
		 * jit+unescape at runtime, as for weak actions.
		 */
		strcpy(unesc_pat, pattern);
		unescape(unesc_pat);
		
		regexp = malloc(sizeof(regex_t)); 
		if (!regexp) {
		    errmsg("malloc");
		    return;
		}
		
		if ((errcode = regcomp((regex_t *)regexp, unesc_pat, REG_EXTENDED))) {
		    int n;
		    char *tmp;
		    n = regerror(errcode, (regex_t *)regexp, NULL, 0);
		    tmp = (char *)malloc(n);
		    if (tmp) {
			if (!regerror(errcode, (regex_t *)regexp, tmp, n))
			    errmsg("regerror");
			else {
			    PRINTF("#regexp error: %s\n", tmp);
			}
			free(tmp);
		    } else {
			error = NO_MEM_ERROR;
			errmsg("malloc");
		    }
		    regfree((regex_t *)regexp);
		    free(regexp);
		    return;
		}
	    }
#endif
            command = p;
	    
            if (hastail) {
                if (np && *np) {
		    change_actionorprompt(*np, pattern, command, type, regexp, onprompt);
                    (*np)->active = active;
                } else {
		    if (onprompt)
			add_new_prompt(label, pattern, command, active, 
				       type, regexp);
		    else
			add_new_action(label, pattern, command, active, 
				       type, regexp);
		}

		if( group != NULL ) {
			/* I don't know why but we need to clip this because somehow
			 * the original string is restored to *p at some point instead
			 * of the null-clipped one we used waaaay at the top. */
        		p = first_regular(group, ' ');
			*p = '\0';
	    		np = lookup_action(label);
			if( (*np)->group != NULL )
				free( (*np)->group );

			if ( *group == '\0' || strcmp(group,"*") == 0 )
				group = NULL;

			(*np) -> group = my_strdup( group );
		}
            }
	    
	    /* '=': list action */
        } else if (assign == '='){
            if (np && *np) {
		int len = (int)strlen((*np)->label);
		sprintf(inserted_next, "#%s %c%c%.*s %.*s=%.*s", ONPROMPT,
			action_chars[(*np)->type], (*np)->active ? '+' : '-',
			BUFSIZE - 6 /*strlen(ONPROMPT)*/ - 7, (*np)->label,
			BUFSIZE - 6 /*strlen(ONPROMPT)*/ - 7 - len, (*np)->pattern,
			BUFSIZE - 6 /*strlen(ONPROMPT)*/ - 7 - len - (int)strlen((*np)->pattern),
			(*np)->command);
            } else {
                PRINTF("#no %s, cannot list label: \"%s\"\n", ONPROMPT, label);
            }
	    
	    /* '+', '-': turn action on/off */
        } else {
            if (np && *np) {
                (*np)->active = (sign == '+');
                if (opt_info) {
                    PRINTF("#%s %c%s %s is now o%s.\n", ONPROMPT,
			   action_chars[(*np)->type],
			   label,
			   (*np)->pattern, (sign == '+') ? "n" : "ff");
                }
            } else {
                PRINTF("#no %s, cannot turn o%s label: \"%s\"\n", ONPROMPT,
		       (sign == '+') ? "n" : "ff", label);
            }
        }
	
	/* anonymous action, cannot be regexp */
    } else {
	
	if (onprompt) {
	    PRINTF("#anonymous prompts not supported.\n#please use labelled prompts.\n");
	    return;
	}
	
        command = first_regular(str, '=');
	
        if (*command == '=') {
            *command = '\0';

	    my_strncpy(pattern, str, BUFSIZE-1);
            *command++ = '=';
            np = lookup_action_pattern(pattern);
            if (*command)
		if (np && *np)
		change_actionorprompt(*np, NULL, command, ACTION_WEAK, NULL, 0);
	    else
		add_anonymous_action(pattern, command, ACTION_WEAK, NULL);
            else if (np && *np)
		delete_action(np);
            else {
                PRINTF("#no action, cannot delete pattern: \"%s\"\n",
		       pattern);
                return;
            }
        } else {
            np = lookup_action_pattern(str);
            if (np && *np) {
                sprintf(inserted_next, "#action %.*s=%.*s",
			BUFSIZE - 10, (*np)->pattern,
			BUFSIZE - (int)strlen((*np)->pattern) - 10,
			(*np)->command);
            } else {
                PRINTF("#no action, cannot show pattern: \"%s\"\n", str);
            }
        }
    }
}

#undef ONPROMPT

/*
 * display attribute syntax
 */
void show_attr_syntax __P0 (void)
{
    int i;
    PRINTF("#attribute syntax:\n\tOne or more of:\tbold, blink, underline, inverse\n\tand/or\t[foreground] [ON background]\n\tColors: ");
    for (i = 0; i < COLORS; i++)
	tty_printf("%s%s", colornames[i],
		 (i == LOWCOLORS - 1 || i == COLORS - 1) ? "\n\t\t" : ",");
    tty_printf("%s\n", colornames[i]);
}

/*
 * put escape sequences to turn on/off an attribute in given buffers
 */
void attr_string __P3 (int,attrcode, char *,begin, char *,end)
{
    int fg = FOREGROUND(attrcode), bg = BACKGROUND(attrcode),
      tok = ATTR(attrcode);
    char need_end = 0;
    *begin = *end = '\0';
    
    if (tok > (ATTR_BOLD | ATTR_BLINK | ATTR_UNDERLINE | ATTR_INVERSE)
	|| fg > COLORS || bg > COLORS || attrcode == NOATTRCODE)
      return;    /* do nothing */
    
    if (fg < COLORS) {
	if (bg < COLORS) {
	    sprintf(begin, "\033[%c%d;%s%dm",
		    fg<LOWCOLORS ? '3' : '9', fg % LOWCOLORS,
		    bg<LOWCOLORS ? "4" :"10", bg % LOWCOLORS);
#ifdef TERM_LINUX
	    strcpy(end, "\033[39;49m");
#endif
	} else {
	    sprintf(begin, "\033[%c%dm",
		    fg<LOWCOLORS ? '3' : '9', fg % LOWCOLORS);
#ifdef TERM_LINUX
	    strcpy(end, "\033[39m");
#endif
	}
    } else if (bg < COLORS) {
	sprintf(begin, "\033[%s%dm", 
		bg<LOWCOLORS ? "4" : "10", bg % LOWCOLORS);
#ifdef TERM_LINUX
	strcpy(end, "\033[49m");
#endif
    }
    
#ifndef TERM_LINUX
    if (fg < COLORS || bg < COLORS)
 	need_end = 1;
#endif

    if (tok & ATTR_BOLD) {
	if (tty_modebold[0]) {
	    strcat(begin, tty_modebold);
#ifdef TERM_LINUX
	    strcat(end, "\033[21m");
#else
	    need_end = 1;
#endif
	} else {
	    strcat(begin, tty_modestandon);
	    strcpy(end, tty_modestandoff);
	}
    }
    
    if (tok & ATTR_BLINK) {
	if (tty_modeblink[0]) {
	    strcat(begin, tty_modeblink);
#ifdef TERM_LINUX
	    strcat(end, "\033[25m");
#else
	    need_end = 1;
#endif
	} else {
	    strcat(begin, tty_modestandon);
	    strcpy(end, tty_modestandoff);
	}
    }
    
    if (tok & ATTR_UNDERLINE) {
	if (tty_modeuline[0]) {
	    strcat(begin, tty_modeuline);
#ifdef TERM_LINUX
	    strcat(end, "\033[24m");
#else
	    need_end = 1;
#endif
	} else {
	    strcat(begin, tty_modestandon);
	    strcpy(end, tty_modestandoff);
	}
    }
    
    if (tok & ATTR_INVERSE) {
	if (tty_modeinv[0]) {
	    strcat(begin, tty_modeinv);
#ifdef TERM_LINUX
	    strcat(end, "\033[27m");
#else
	    need_end = 1;
#endif
	} else {
	    strcat(begin, tty_modestandon);
	    strcpy(end, tty_modestandoff);
	}
    }

#ifndef TERM_LINUX
    if (need_end)
	strcpy(end, tty_modenorm);
#endif
}

/*
 * parse attribute description in line.
 * Return attribute if successful, -1 otherwise.
 */
int parse_attributes __P1 (char *,line)
{
    char *p;
    int tok = 0, fg, bg, t = -1;
    
    if (!(p = strtok(line, " ")))
      return NOATTRCODE;
    
    fg = bg = NO_COLOR;
    
    while (t && p) {
	if (!strcasecmp(p, "bold"))
	  t = ATTR_BOLD;
	else if (!strcasecmp(p, "blink"))
	  t = ATTR_BLINK;
	else if (!strcasecmp(p, "underline"))
	  t = ATTR_UNDERLINE;
	else if (!strcasecmp(p, "inverse") || !strcasecmp(p, "reverse"))
	  t = ATTR_INVERSE;
	else
	  t = 0;
	
	if (t) {	 
	    tok |= t;
	    p = strtok(NULL, " ");
	}
    }
    
    if (!p)
	return ATTRCODE(tok, fg, bg);
    
    for (t = 0; t <= COLORS && strcmp(p, colornames[t]); t++)
	;
    if (t <= COLORS) {
	fg = t;
	p = strtok(NULL, " ");
    }
    
    if (!p)
      return ATTRCODE(tok, fg, bg);
    
    if (strcasecmp(p, "on"))
      return -1;      /* invalid attribute */
    
    if (!(p = strtok(NULL, " ")))
      return -1;
    
    for (t = 0; t <= COLORS && strcmp(p, colornames[t]); t++)
      ;
    if (t <= COLORS)
      bg = t;
    else
      return -1;
    
    return ATTRCODE(tok, fg, bg);
}


/*
 * return a static pointer to name of given attribute code
 */
char *attr_name __P1 (int,attrcode)
{
    static char name[BUFSIZE];
    int fg = FOREGROUND(attrcode), bg = BACKGROUND(attrcode), tok = ATTR(attrcode);
    
    name[0] = 0;
    if (tok > (ATTR_BOLD | ATTR_BLINK | ATTR_UNDERLINE | ATTR_INVERSE) || fg > COLORS || bg > COLORS)
      return name;   /* error! */
    
    if (tok & ATTR_BOLD)
      strcat(name, "bold ");
    if (tok & ATTR_BLINK)
      strcat(name, "blink ");
    if (tok & ATTR_UNDERLINE)
      strcat(name, "underline ");
    if (tok & ATTR_INVERSE)
      strcat(name, "inverse ");
    
    if (fg < COLORS || (fg == bg && fg == COLORS && !tok))
      strcat(name, colornames[fg]);
    
    if (bg < COLORS) {
	strcat(name, " on ");
	strcat(name, colornames[bg]);
    }
    
    if (!*name)
	strcpy(name, "none");
    
    return name;
}

/*
 * show defined marks
 */
void show_marks __P0 (void)
{
    marknode *p;
    PRINTF("#%s marker%s defined%c\n", markers ? "the following" : "no",
	       (markers && !markers->next) ? " is" : "s are",
	       markers ? ':' : '.');
    for (p = markers; p; p = p->next)
	tty_printf("#mark %s%s=%s\n", p->mbeg ? "^" : "",
		   p->pattern, attr_name(p->attrcode));
}


/*
 * parse arguments to the #mark command
 */
void parse_mark __P1 (char *,str)
{
    char *p;
    marknode **np, *n;
    char mbeg = 0;
    
    if (*str == '=') {
	PRINTF("#marker must be non-null.\n");
	return;
    }
    p = first_regular(str, '=');
    if (!*p) {
	if (*str ==  '^')
	    mbeg = 1, str++;
        unescape(str);
        np = lookup_marker(str, mbeg);
        if ((n = *np)) {
	    ptr pbuf = (ptr)0;
	    char *name;
	    pbuf = ptrmescape(pbuf, n->pattern, strlen(n->pattern), 0);
	    if (MEM_ERROR) { ptrdel(pbuf); return; }
            name = attr_name(n->attrcode);
            sprintf(inserted_next, "#mark %s%.*s=%.*s", n->mbeg ? "^" : "",
		    BUFSIZE-(int)strlen(name)-9, pbuf ? ptrdata(pbuf) : "",
		    BUFSIZE-9, name);
	    ptrdel(pbuf);
        } else {
            PRINTF("#unknown marker, cannot show: \"%s\"\n", str);
        }
	
    } else {
        int attrcode, wild = 0;
	char pattern[BUFSIZE], *p2;

        *(p++) = '\0';
	p = skipspace(p);
	if (*str ==  '^')
	    mbeg = 1, str++;
	my_strncpy(pattern, str, BUFSIZE-1);
        unescape(pattern);
	p2 = pattern;
	while (*p2) {
	    if (ISMARKWILDCARD(*p2)) {
		wild = 1;
		if (ISMARKWILDCARD(*(p2 + 1))) {
		    error=SYNTAX_ERROR;
		    PRINTF("#error: two wildcards (& or $) may not be next to eachother\n");
		    return;
		}
	    }
	    p2++;
	}

        np = lookup_marker(pattern, mbeg);
        attrcode = parse_attributes(p);
        if (attrcode == -1) {
            PRINTF("#invalid attribute syntax.\n");
	    error=SYNTAX_ERROR;
            if (opt_info) show_attr_syntax();
        } else if (!*p)
	    if ((n = *np)) {
		if (opt_info) {
		    PRINTF("#deleting mark: %s%s=%s\n", n->mbeg ? "^" : "",
			   n->pattern, attr_name(n->attrcode));
		}
		delete_marknode(np);
	    } else {
		PRINTF("#unknown marker, cannot delete: \"%s%s\"\n",
		       mbeg ? "^" : "", pattern);
	    }
        else {
            if (*np) {
                (*np)->attrcode = attrcode;
                if (opt_info) {
                    PRINTF("#changed");
                }
            } else {
                add_marknode(pattern, attrcode, mbeg, wild);
                if (opt_info) {
                    PRINTF("#new");
                }
            }
            if (opt_info)
		tty_printf(" mark: %s%s=%s\n", mbeg ? "^" : "",
			   pattern, attr_name(attrcode));
        }
    }
}

/*
 * turn ASCII description of a sequence
 * into raw escape sequence
 * return pointer to end of ASCII description
 */
static char *unescape_seq __P3 (char *,buf, char *,seq, int *,seqlen)
{
    char c, *start = buf;
    enum { NORM, ESCSINGLE, ESCAPE, CARET, DONE } state = NORM;
    
    for (; (c = *seq); seq++) {
	switch (state) {
	  case NORM:
	    if (c == '^')
		state = CARET;
	    else if (c == ESC)
		state = ESCSINGLE;
	    else if (c == '=')
		state = DONE;
	    else
		*(buf++) = c;
	    break;
	  case CARET:
	    /*
	     * handle ^@ ^A  ... ^_ as expected:
	     * ^@ == 0x00, ^A == 0x01, ... , ^_ == 0x1f
	     */
	    if (c > 0x40 && c < 0x60)
		*(buf++) = c & 0x1f;
	    /* special case: ^? == 0x7f */
	    else if (c == '?')
		*(buf++) = 0x7f;
	    state = NORM;
	    break;
	  case ESCSINGLE:
	  case ESCAPE:
	    /*
	     * GH: \012 ==> octal number
	     */
	    if (state == ESCSINGLE &&
		ISODIGIT(seq[0]) && ISODIGIT(seq[1]) && ISODIGIT(seq[2])) {
		*(buf++) =(((seq[0] - '0') << 6) | 
			   ((seq[1] - '0') << 3) |
			   (seq[2] - '0'));
		seq += 2;
	    } else {
		*(buf++) = c;
		if (c == ESC)
		    state = ESCAPE;
		else
		    state = NORM;
	    }
	    break;
	  default:
	    break;
	}
	if (state == DONE)
	    break;
    }
    *buf = '\0';
    *seqlen = buf - start;
    return seq;
}

/*
 * read a single character from tty, with timeout in milliseconds.
 * timeout == 0 means wait indefinitely (no timeout).
 * return char or -1 if timeout was reached.
 */
static int get_one_char __P1 (int,timeout)
{
    struct timeval timeoutbuf;
    fd_set fds;
    int n;
    char c;
    
 again:
    FD_ZERO(&fds);
    FD_SET(tty_read_fd, &fds);
    timeoutbuf.tv_sec = 0;
    timeoutbuf.tv_usec = timeout * uSEC_PER_mSEC;
    n = select(tty_read_fd + 1, &fds, NULL, NULL,
               timeout ? &timeoutbuf : NULL);
    if (n == -1 && errno == EINTR)
	return -1;
    if (n == -1) {
	errmsg("select");
	return -1;
    }
    do {
        n = tty_read(&c, 1);
    } while (n < 0 && errno == EINTR);
    if (n == 1)
        return (unsigned char)c;
    if (n < 0) {
        if (errno != EAGAIN)
	    errmsg("read from tty");
        return -1;
    }
    /* n == 0 */
    if (timeout == 0)
	goto again;
    return -1;
}

/*
 * print an escape sequence in human-readably form.
 */
void print_seq __P2 (char *,seq, int,len)
{
    while (len--) {
	unsigned char ch = *(seq++);
        if (ch == '\033') {
            tty_puts("esc ");
            continue;
        }
        if (ch < ' ') {
            tty_putc('^');
            ch |= '@';
        }
        if (ch == ' ')
	    tty_puts("space ");
        else if (ch == 0x7f)
	    tty_puts("del ");
	else if (ch & 0x80)
	    tty_printf("\\%03o ", ch);
	else
	    tty_printf("%c ", ch);
    }
}

/*
 * return a static pointer to escape sequence made printable, for use in
 * definition-files
 */
char *seq_name __P2 (char *,seq, int,len)
{
    static char buf[CAPLEN*4];
    char *p = buf;
    /*
     * rules: control chars are written as ^X, where
     * X is char | 64
     * 
     * GH: codes > 0x80  ==>  octal \012
     *
     * special case: 0x7f is written ^?
     */
    while (len--) {
        unsigned char c = *seq++;
        if (c == '^' || (c && strchr(SPECIAL_CHARS, c)))
	    *(p++) = ESC;
	
	if (c < ' ') {
            *(p++) = '^';
            *(p++) = c | '@';
	} else if (c == 0x7f) {
	    *(p++) = '^';
	    *(p++) = '?';
        } else if (c & 0x80) {
	    /* GH: save chars with high bit set in octal */
	    sprintf(p, "\\%03o", (int)c);
	    p += strlen(p);
	} else
	    *(p++) = c;
    }
    *p = '\0';
    return buf;
}

/*
 * read a single escape sequence from the keyboard
 * prompting user for it; return static pointer
 */
char *read_seq __P2 (char *,name, int *,len)
{
    static char seq[CAPLEN];
    int i = 1, tmp;
    
    PRINTF("#please press the key \"%s\" : ", name);
    tty_flush();

    if ((tmp = get_one_char(0)) >= 0)
	seq[0] = tmp;
    else {
	tty_puts("#unable to get key. Giving up.\n");
	return NULL;
    }
    
    while (i < CAPLEN - 1 &&
	   (tmp = get_one_char(KBD_TIMEOUT)) >= 0)
	seq[i++] = tmp;
    *len = i;
    print_seq(seq, i);
    
    tty_putc('\n');
    if (seq[0] >= ' ' && seq[0] <= '~') {
	PRINTF("#that is not a redefinable key.\n");
	return NULL;
    }
    return seq;
}

/*
 * show full definition of one binding,
 * with custom message
 */
static void show_single_bind __P2 (char *,msg, keynode *,p)
{
    if (p->funct == key_run_command) {
	PRINTF("#%s %s %s=%s\n", msg, p->name,
	       seq_name(p->sequence, p->seqlen), p->call_data);
    } else {
	PRINTF("#%s %s %s=%s%s%s\n", msg, p->name,
	       seq_name(p->sequence, p->seqlen),
	       internal_functions[lookup_edit_function(p->funct)].name,
	       p->call_data ? " " : "",
	       p->call_data ? p->call_data : "");
    }
}

/*
 * list keyboard bindings
 */
void show_binds __P1 (char,edit)
{
    keynode *p;
    int count = 0;
    for (p = keydefs; p; p = p->next) {
	if (edit != (p->funct == key_run_command)) {
	    if (!count) {
		if (edit) {
		    PRINTF("#line-editing keys:\n");
		} else {
		    PRINTF("#user-defined keys:\n");
		}
	    }
	    show_single_bind("bind", p);
	    ++count;
	}
    }
    if (!count) {
        PRINTF("#no key bindings defined right now.\n");
    }
}


/*
 * interactively create a new keybinding
 */
static void define_new_key __P2 (char *,name, char *,command)
{
    char *seq, *arg;
    keynode *p;
    int seqlen, function;
    
    seq = read_seq(name, &seqlen);
    if (!seq) return;
    
    for (p = keydefs; p; p = p->next)
	/* GH: don't allow binding of supersets of another bind */
	if (!memcmp(p->sequence, seq, MIN2(p->seqlen, seqlen))) {
	    show_single_bind("key already bound as:", p);
	    return;
	}
    
    function = lookup_edit_name(command, &arg);
    if (function)
	add_keynode(name, seq, seqlen,
		    internal_functions[function].funct, arg);
    else
	add_keynode(name, seq, seqlen, key_run_command, command);
    
    if (opt_info) {
	PRINTF("#new key binding: %s %s=%s\n",
	       name, seq_name(seq, seqlen), command);
    }
}

/*
 * parse the #bind command non-interactively.
 */
static void parse_bind_noninteractive __P1 (char *,arg)
{
    char rawseq[CAPLEN], *p, *seq, *params;
    int function, seqlen;
    keynode **kp;
    
    p = strchr(arg, ' ');
    if (!p) {
        PRINTF("#syntax error: \"#bind %s\"\n", arg);
        return;
    }
    *(p++) = '\0';
    seq = p = skipspace(p);
    
    p = unescape_seq(rawseq, p, &seqlen);
    if (!p[0] || !p[1]) {
        PRINTF("#syntax error: \"#bind %s %s\"\n", arg, seq);
        return;
    }
    *p++ = '\0';
    
    kp = lookup_key(arg);
    if (kp && *kp)
	delete_keynode(kp);
    
    if ((function = lookup_edit_name(p, &params)))
	add_keynode(arg, rawseq, seqlen, 
		    internal_functions[function].funct, params);
    else
	add_keynode(arg, rawseq, seqlen, key_run_command, p);
    
    if (opt_info) {
	PRINTF("#%s: %s %s=%s\n",
               (kp && *kp) ? "redefined key" : "new key binding",
               arg, seq, p);
    }
}

/*
 * parse the argument of the #bind command (interactive)
 */
void parse_bind __P1 (char *,arg)
{
    char *p, *q, *command, *params;
    char *name = arg;
    keynode **npp, *np;
    int function;
    
    p = first_valid(arg, '=');
    q = first_valid(arg, ' ');
    q = skipspace(q);
    
    if (*p && *q && p > q) {	
        parse_bind_noninteractive(arg);
	return;
    }
    
    if (*p) {
        *(p++) = '\0';
        np = *(npp = lookup_key(name));
        if (*p) {
            command = p;
            if (np) {
                if (np->funct == key_run_command)
		    free(np->call_data);
		if ((function = lookup_edit_name(command, &params))) {
		    np->call_data = my_strdup(params);
		    np->funct = internal_functions[function].funct;
		} else {
                    np->call_data = my_strdup(command);
		    np->funct = key_run_command;
		}
                if (opt_info) {
                    PRINTF("#redefined key: %s %s=%s\n", name,
			       seq_name(np->sequence, np->seqlen),
			       command);
                }
            } else
		define_new_key(name, command);
        } else {
            if (np) {
                if (opt_info)
		    show_single_bind("deleting key binding:", np);
		delete_keynode(npp);
            } else {
                PRINTF("#no such key: \"%s\"\n", name);
            }
        }
    } else {
        np = *(npp = lookup_key(name));
        if (np) {
	    char *seqname;
	    int seqlen;
	    seqname = seq_name(np->sequence, np->seqlen);
	    seqlen = strlen(seqname);
	    
	    if (np->funct == key_run_command)
		sprintf(inserted_next, "#bind %.*s %s=%.*s",
			BUFSIZE-seqlen-9, name, seqname,
			BUFSIZE-seqlen-(int)strlen(name)-9,
			np->call_data);
	    else {
		p = internal_functions[lookup_edit_function(np->funct)].name;
		sprintf(inserted_next, "#bind %.*s %s=%s%s%.*s",
			BUFSIZE-seqlen-10, name, seqname, p,
			np->call_data ? " " : "",
			BUFSIZE-seqlen-(int)strlen(name)-(int)strlen(p)-10,
			np->call_data ? np->call_data : "");
	    }
	} else {
            PRINTF("#no such key: \"%s\"\n", name);
        }
    }
}

void parse_rebind __P1 (char *,arg)
{
    char rawseq[CAPLEN], *seq, **old;
    keynode **kp, *p;
    int seqlen;
    
    arg = skipspace(arg);
    if (!*arg) {
	PRINTF("#rebind: missing key.\n");
	return;
    }
    
    seq = first_valid(arg, ' ');
    if (*seq) {
	*seq++ = '\0';
	seq = skipspace(seq);
    }
    
    kp = lookup_key(arg);
    if (!kp || !*kp) {
	PRINTF("#no such key: \"%s\"\n", arg);
	return;
    }
    
    if (!*seq) {
	seq = read_seq(arg, &seqlen);
	if (!seq)
	    return;
    } else {
	(void)unescape_seq(rawseq, seq, &seqlen);
	seq = rawseq;
    }

    for (p = keydefs; p; p = p->next) {
	if (p == *kp)
	    continue;
	if (!memcmp(p->sequence, seq, MIN2(seqlen, p->seqlen))) {
	    show_single_bind("key already bound as:", p);
	    return;
	}
    }
    
    old = &((*kp)->sequence);
    if (*old)
	free(*old);
    *old = (char *)malloc((*kp)->seqlen = seqlen);
    memmove(*old, seq, seqlen);

    if (opt_info)
	show_single_bind("redefined key:", *kp);
}

/*
 * evaluate an expression, or unescape a text.
 * set value of start and end line if <(expression...) or !(expression...)
 * if needed, use/malloc "pbuf" as buffer (on error, also free pbuf)
 * return resulting char *
 */
char *redirect __P7 (char *,arg, ptr *,pbuf, char *,kind, char *,name, int,also_num, long *,start, long *,end)
{
    char *tmp = skipspace(arg), k;
    int type, i;
    
    if (!pbuf) {
	print_error(error=INTERNAL_ERROR);
	return NULL;
    }
    
    k = *tmp;
    if (k == '!' || k == '<')
	arg = ++tmp;
    else
	k = 0;
    
    *start = *end = 0;
    
    if (*tmp=='(') {

	arg = tmp + 1;
	type = evalp(pbuf, &arg);
	if (!REAL_ERROR && type!=TYPE_TXT && !also_num)
	    error=NO_STRING_ERROR;
	if (REAL_ERROR) {
	    PRINTF("#%s: ", name);
	    print_error(error);
	    ptrdel(*pbuf);
	    return NULL;
	}
	for (i=0; i<2; i++) if (*arg == CMDSEP) {
	    long buf;
	    
	    arg++;
	    if (!i && *arg == CMDSEP) {
		*start = 1;
		continue;
	    }
	    else if (i && *arg == ')') {
		*end = LONG_MAX;
		continue;
	    }
	    
	    type = evall(&buf, &arg);
	    if (!REAL_ERROR && type != TYPE_NUM)
		error=NO_NUM_VALUE_ERROR;
	    if (REAL_ERROR) {
		PRINTF("#%s: ", name);
		print_error(error);
		ptrdel(*pbuf);
		return NULL;
	    }
	    if (i)
		*end = buf;
	    else
		*start = buf;
	}
	if (*arg != ')') {
	    PRINTF("#%s: ", name);
	    print_error(error=MISSING_PAREN_ERROR);
	    ptrdel(*pbuf);
	    return NULL;
	}
	if (!*pbuf) {
	    /* make space to add a final \n */
	    *pbuf = ptrsetlen(*pbuf, 1);
	    ptrzero(*pbuf);
	    if (REAL_ERROR) {
		print_error(error);
		ptrdel(*pbuf);
		return NULL;
	    }
	}
	arg = ptrdata(*pbuf);
	if (!*start && *end)
	    *start = 1;
    } else
	unescape(arg);
    
    *kind = k;
    return arg;
}

void show_vars __P0 (void)
{    
    varnode *v;
    int i, type;
    ptr p = (ptr)0;
    
    PRINTF("#the following variables are defined:\n");
    
    for (type = 0; !REAL_ERROR && type < 2; type++) {
	reverse_sortedlist((sortednode **)&sortednamed_vars[type]);
	v = sortednamed_vars[type];
	while (v) {
	    if (type == 0) {
		tty_printf("#(@%s = %ld)\n", v->name, v->num);
	    } else {
		p = ptrescape(p, v->str, 0);
		if (REAL_ERROR) {
		    print_error(error);
		    break;
		}
		tty_printf("#($%s = \"%s\")\n", v->name,
		       p ? ptrdata(p) : "");
	    }
	    v = v->snext;
	}
	reverse_sortedlist((sortednode **)&sortednamed_vars[type]);
    }
    for (i = -NUMVAR; !REAL_ERROR && i < NUMPARAM; i++) {
	if (*VAR[i].num)
	    tty_printf("#(@%d = %ld)\n", i, *VAR[i].num);
    }
    for (i = -NUMVAR; !REAL_ERROR && i < NUMPARAM; i++) {
	if (*VAR[i].str && ptrlen(*VAR[i].str)) {
	    p = ptrescape(p, *VAR[i].str, 0);
	    if (p && ptrlen(p))
		tty_printf("#($%d = \"%s\")\n", i, ptrdata(p));
	}
    }
    ptrdel(p);
}

void show_delaynode __P2 (delaynode *,p, int,in_or_at)
{
    long d;
    struct tm *s;
    char buf[BUFSIZE];

    update_now();
    d = diff_vtime(&p->when, &now);
    s = localtime((time_t *)&p->when.tv_sec); 
    /* s now points to a calendar struct */
    if (in_or_at) {
	
	if (in_or_at == 2) {
	    /* write time in buf */
	    (void)strftime(buf, BUFSIZE - 1, "%H%M%S", s);
	    sprintf(inserted_next, "#at %.*s (%s) %.*s",
		    BUFSIZE - 15, p->name, buf,
		    BUFSIZE - 15 - (int)strlen(p->name), p->command);
	}
	else
	    sprintf(inserted_next, "#in %.*s (%ld) %.*s",
		    BUFSIZE - LONGLEN - 9, p->name, d,
		    BUFSIZE - LONGLEN - 9 - (int)strlen(p->name), p->command);
    } else {
	(void)strftime(buf, BUFSIZE - 1, "%H:%M:%S", s);
	PRINTF("#at (%s) #in (%ld) \"%s\" %s\n", buf, d, p->name, p->command);
    }
}

void show_delays __P0 (void)
{
    delaynode *p;
    int n = (delays ? delays->next ? 2 : 1 : 0) +
	(dead_delays ? dead_delays->next ? 2 : 1 : 0);
    
    PRINTF("#%s delay label%s defined%c\n", n ? "the following" : "no",
	       n == 1 ? " is" : "s are", n ? ':' : '.');
    for (p = delays; p; p = p->next)
	show_delaynode(p, 0);
    for (p = dead_delays; p; p = p->next)
	show_delaynode(p, 0);
}

void change_delaynode __P3 (delaynode **,p, char *,command, long,millisec)
{
    delaynode *m=*p;
    
    *p = m->next;
    m->when.tv_usec = (millisec % mSEC_PER_SEC) * uSEC_PER_mSEC;
    m->when.tv_sec  =  millisec / mSEC_PER_SEC;
    update_now();
    add_vtime(&m->when, &now);
    if (*command) {
	if (strlen(command) > strlen(m->command)) {
	    free((void*)m->command);
	    m->command = my_strdup(command);
	}
	else
	    strcpy(m->command, command);
    }
    if (millisec < 0)
	add_node((defnode*)m, (defnode**)&dead_delays, rev_time_sort);
    else
	add_node((defnode*)m, (defnode**)&delays, time_sort);
    if (opt_info) {
	PRINTF("#changed ");
	show_delaynode(m, 0);
    }
}

void new_delaynode __P3 (char *,name, char *,command, long,millisec)
{
    vtime t;
    delaynode *node;
    
    t.tv_usec = (millisec % mSEC_PER_SEC) * uSEC_PER_mSEC;
    t.tv_sec  =  millisec / mSEC_PER_SEC;
    update_now();
    add_vtime(&t, &now);
    node = add_delaynode(name, command, &t, millisec < 0);
    if (opt_info && node) {
	PRINTF("#new ");
	show_delaynode(node, 0);
    }
}

void show_history __P1 (int,count)
{
    int i = curline;
    
    if (!count) count = lines - 1;
    if (count >= MAX_HIST) count = MAX_HIST - 1;
    i -= count;
    if (i < 0) i += MAX_HIST;
    
    while (count) {
	if (hist[i]) {
	    PRINTF("#%2d: %s\n", count, hist[i]);
	}
	count--;
	if (++i == MAX_HIST) i = 0;
    }
}

void exe_history __P1 (int,count)
{
    int i = curline;
    char buf[BUFSIZE];
    
    if (count >= MAX_HIST)
	count = MAX_HIST - 1;
    i -= count;
    if (i < 0)
	i += MAX_HIST;
    if (hist[i]) {
	strcpy(buf, hist[i]);
	parse_user_input(buf, 0);
    }
}

