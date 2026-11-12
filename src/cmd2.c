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

#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <signal.h>
#include <ctype.h>
#include <time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include <errno.h>

#include "defines.h"
#include "main.h"
#include "feature/regex.h"
#include "utils.h"
#include "beam.h"
#include "edit.h"
#include "list.h"
#include "map.h"
#include "tcp.h"
#include "tty.h"
#include "eval.h"
#include "cmd2.h"

/* show the alias 'p' as #alias defines it */
static void show_alias(const aliasnode *p)
{
    char buf[BUFSIZE];
    escape_specials(buf, p->name);
    tty_message("#alias %s%s%s%s=%s\n",
                p->active ? "" : "(disabled) ",
                buf, group_delim, p->group == NULL ? "*" : p->group,
                p->subst);
}

/*
 * show defined aliases
 */
void show_aliases(void)
{
    message_printf("#%s alias%s defined%c\n",
                   sortedaliases != NULL ? "the following" : "no",
                   (sortedaliases != NULL && sortedaliases->snext == NULL
                    ? " is"
                    : "es are"),
                   sortedaliases != NULL ? ':' : '.');
    for (aliasnode *p = sortedaliases; p != NULL; p = p->snext)
        show_alias(p);
}

/*
 * check if an alias name contains dangerous things.
 * return true if illegal name (and print reason).
 * if valid, print a warning for unbalanced () {} and ""
 */
static bool check_alias(char *name)
{
    if (*name == '\0') {
        message_printf("#illegal alias name: is empty!\n");
        error = INVALID_NAME_ERROR;
        return true;
    }
    if (*name == '{') {
        wrap_printf("#illegal beginning '{' in alias name: \"%s\"", name);
        error = INVALID_NAME_ERROR;
        return true;
    }
    if (strchr(name, ' ') != NULL) {
        wrap_printf("#illegal spaces in alias name: \"%s\"", name);
        error = INVALID_NAME_ERROR;
        return true;
    }

    enum { NORM, ESCAPE } state = NORM;
    bool quotes = false, ok = true;
    int paren = 0, braces = 0;
    for (char *p = name; ok && *p != '\0'; ++p) {
        char c = *p;
        switch (state) {
        case NORM:
            if (c == ESC)
                state = ESCAPE;
            else if (quotes) {
                if (c == '\"')
                    quotes = false;
            } else if (c == '\"')
                quotes = true;
            else if (c == ')')
                --paren;
            else if (c == '(')
                ++paren;
            else if (c == '}')
                --braces;
            else if (c == '{')
                ++braces;
            else if (c == CMDSEP && paren == 0 && braces == 0)
                ok = false;
            break;
        case ESCAPE:
            if (c == ESC)
                state = ESCAPE;
            else
                state = NORM;
            break;
        default:
            break;
        }
    }

    if (!ok) {
        wrap_printf("#illegal non-escaped ';' in alias name: \"%s\"", name);
        error = INVALID_NAME_ERROR;
        return true;
    }

    if (quotes || paren != 0 || braces != 0) {
        wrap_printf("#warning: unbalanced%s%s%s in alias name \"%s\"",
                    quotes ? " \"\"" : "",
                    paren != 0 ? " ()" : "", braces != 0 ? " {}" : "", name);
    }

    return false;
}

/*
 * parse the #alias command
 */
void parse_alias(char *str)
{
    char *left = skipspace(str);

    str = first_valid(left, '=');

    if (*str == '=') {
        *str = '\0';
        ++str;
        char *right = str;
        unescape(left);

        /* break out group name (if present) */
        char *group = strstr(left, group_delim);
        if (group != NULL) {
            *group = '\0';
            group += strlen(group_delim);
        }

        if (check_alias(left))
            return;
        aliasnode **np = lookup_alias(left);
        aliasnode *p = *np;
        if (*str == '\0') {
            /* delete alias */
            if (p != NULL) {
                if (opt_info) {
                    message_printf("#deleting alias: %s=%s\n", left, p->subst);
                }
                delete_aliasnode(np);
            } else {
                wrap_printf("#unknown alias, cannot delete: \"%s\"", left);
            }
        } else {
            /* add/redefine alias */

            /* an alias CAN be defined by itself (direct recursion) */
            if (p != NULL) {
                free(p->subst);
                p->subst = my_strdup(right);
            } else {
                add_aliasnode(left, right);
            }

            /* get alias again to add group (if needed)
             * don't take the lookup penalty though if not changing groups */
            if (group != NULL) {
                np = lookup_alias(left);
                if (*np == NULL)
                    return;     /* out of memory when adding it */
                if ((*np)->group != NULL)
                    free((*np)->group);

                if (*group == '\0' || strcmp(group, "*") == 0)
                    group = NULL;

                (*np)->group = my_strdup(group);
            }

            if (opt_info) {
                message_printf("#%s alias in group '%s': %s=%s\n",
                               p != NULL ? "changed" : "new",
                               group == NULL ? "*" : group, left, right);
            }
        }
    } else {
        /* show alias */

        *str = '\0';
        unescape(left);
        if (check_alias(left))
            return;
        aliasnode **np = lookup_alias(left);
        if (*np != NULL) {
            char buf[BUFSIZE];
            escape_specials(buf, left);
            fmt_next_edit("#alias %s%s%s=%s",
                          buf,
                          group_delim,
                          (*np)->group == NULL ? "*" : (*np)->group,
                          (*np)->subst);
        } else {
            wrap_printf("#unknown alias, cannot show: \"%s\"", left);
        }
    }
}

/*
 * delete an action node
 */
static void delete_action(actionnode **nodep)
{
    if (opt_info) {
        message_printf("#deleting action: >%c%s %s\n",
                       (*nodep)->active ? '+' : '-', (*nodep)->label,
                       (*nodep)->pattern);
    }
    delete_actionnode(nodep);
}

/*
 * delete a prompt node
 */
static void delete_prompt(actionnode **nodep)
{
    if (opt_info) {
        message_printf("#deleting prompt: >%c%s %s\n",
                       (*nodep)->active ? '+' : '-', (*nodep)->label,
                       (*nodep)->pattern);
    }
    delete_promptnode(nodep);
}

/*
 * create new action
 */
static void add_new_action(char *label, char *pattern, char *command,
                           bool active, int type, void *q)
{
    add_actionnode(pattern, command, label, active, type, q);
    if (opt_info) {
        message_printf("#new action: %c%c%s %s=%s\n",
                       action_chars[type],
                       active ? '+' : '-', label,
                       pattern, command);
    }
}

/*
 * create new prompt
 */
static void add_new_prompt(char *label, char *pattern, char *command,
                           bool active, int type, void *q)
{
    add_promptnode(pattern, command, label, active, type, q);
    if (opt_info) {
        message_printf("#new prompt: %c%c%s %s=%s\n",
                       action_chars[type],
                       active ? '+' : '-', label,
                       pattern, command);
    }
}

/*
 * add an action with numbered label
 */
static void add_anonymous_action(char *pattern, char *command, int type,
                                 void *q)
{
    static int last = 0;
    char label[16];
    do {
        sprintf(label, "%d", ++last);
    } while (*lookup_action(label) != NULL);
    add_new_action(label, pattern, command, true, type, q);
}

#define ONPROMPT (onprompt ? "prompt" : "action")

/*
 * change fields of an existing action node: 'pattern', with its compiled
 * regexp 'q', or 'command' can be NULL if they do not change
 */
static void change_actionorprompt(actionnode *node, char *pattern,
                                  char *command, int type, void *q,
                                  bool onprompt)
{
    if (pattern != NULL) {
#ifdef USE_REGEXP
        if (node->type == ACTION_REGEXP && node->regexp != NULL) {
            regfree(node->regexp);
            free(node->regexp);
        }
        node->regexp = q;
#endif
        free(node->pattern);
        node->pattern = my_strdup(pattern);
        node->type = type;
    }
    if (command != NULL) {
        free(node->command);
        node->command = my_strdup(command);
    }

    if (opt_info) {
        message_printf("#changed %s %c%c%s %s=%s\n", ONPROMPT,
                       action_chars[node->type],
                       node->active ? '+' : '-',
                       node->label, node->pattern, node->command);
    }
}

/* show the action 'p' as #action defines it */
static void show_action(const actionnode *p)
{
    tty_message("#action %c%c%s%s%s %s=%s\n",
                action_chars[p->type],
                p->active ? '+' : '-', p->label,
                group_delim,
                p->group == NULL ? "*" : p->group,
                p->pattern,
                p->command);
}

/*
 * show defined actions
 */
void show_actions(void)
{
    message_printf("#%s action%s defined%c\n",
                   actions != NULL ? "the following" : "no",
                   (actions != NULL && actions->next == NULL
                    ? " is"
                    : "s are"),
                   actions != NULL ? ':' : '.');
    for (const actionnode *p = actions; p != NULL; p = p->next)
        show_action(p);
}

/* is 'g', the group of an alias or action, the group 'group'? */
static bool in_group(const char *g, const char *group)
{
    return g != NULL && strcmp(g, group) == 0;
}

/*
 * show the aliases and actions of the group 'group', those that
 * #group <group> on and off turn on and off
 */
void show_group(const char *group)
{
    int count = 0;
    for (const aliasnode *p = sortedaliases; p != NULL; p = p->snext)
        count += in_group(p->group, group) ? 1 : 0;
    for (const actionnode *p = actions; p != NULL; p = p->next)
        count += in_group(p->group, group) ? 1 : 0;
    if (count == 0) {
        message_printf("#no aliases or actions are in group %s.\n", group);
        return;
    }
    message_printf("#the following %s in group %s:\n",
                   count == 1 ? "is" : "are", group);
    for (const aliasnode *p = sortedaliases; p != NULL; p = p->snext) {
        if (in_group(p->group, group))
            show_alias(p);
    }
    for (const actionnode *p = actions; p != NULL; p = p->next) {
        if (in_group(p->group, group))
            show_action(p);
    }
}

/*
 * show defined prompts
 */
void show_prompts(void)
{
    message_printf("#%s prompt%s defined%c\n",
                   prompts != NULL ? "the following" : "no",
                   (prompts != NULL && prompts->next == NULL
                    ? " is"
                    : "s are"),
                   prompts != NULL ? ':' : '.');
    for (promptnode *p = prompts; p != NULL; p = p->next)
        tty_message("#prompt %c%c%s %s=%s\n",
                    action_chars[p->type],
                    p->active ? '+' : '-', p->label,
                    p->pattern, p->command);
}

/*
 * parse the #action and #prompt commands
 * this function is too damn complex because of the hairy syntax. it should be
 * split up or rewritten as an fsm instead.
 */
void parse_action(char *str, bool onprompt)
{
    char *p = skipspace(str);
    char sign = *p;
    if (sign == '\0') {
        message_printf("%s: no arguments given\n", ONPROMPT);
        return;
    }

    str = p + 1;

    char assign;
    char type = ACTION_WEAK;
    switch (sign) {
    case '+':
    case '-':           /* edit */
    case '<':           /* delete */
    case '=':           /* list */
        assign = sign;
        break;
    case '%':           /* action_chars[ACTION_REGEXP] */
        type = ACTION_REGEXP;
        /* fall through */
    case '>':           /* action_chars[ACTION_WEAK] */
        /* define/edit */
        assign = '>';
        sign = *(p + 1);
        if (sign == '\0') {
            message_printf("#%s: label expected\n", ONPROMPT);
            return;
        } else if (sign == '+' || sign == '-') {
            ++str;
        } else {
            sign = '+';
        }
        break;
    default:
        assign = '\0';  /* anonymous action */
        str = p;
        break;
    }

    if (assign != '\0') {
        /* labelled action */
        p = first_regular(str, ' ');
        bool hastail = *p != '\0';
        if (hastail)
            *p = '\0';

        /* break out group name (if present) */
        char *group = strstr(str, group_delim);
        if (group != NULL) {
            *group = '\0';
            group += strlen(group_delim);
        }

        char label[BUFSIZE];
        my_strncpy(label, str, BUFSIZE - 1);
        if (hastail)
            *p++ = ' '; /* p points to start of pattern, or to \0 */

        if (*label == '\0') {
            message_printf("#%s: label expected\n", ONPROMPT);
            return;
        }

        actionnode **np = (onprompt
                           ? lookup_prompt(label)
                           : lookup_action(label));

        if (assign == '<') {
            /* '<' : remove action */
            if (np == NULL || *np == NULL) {
                wrap_printf("#no %s, cannot delete label: \"%s\"", ONPROMPT,
                            label);
            } else if (onprompt) {
                delete_prompt(np);
            } else {
                delete_action(np);
            }
        } else if (assign == '>') {
            /* '>' : define action */
#ifndef USE_REGEXP
            if (type == ACTION_REGEXP) {
                message_printf("#error: regexp not allowed\n");
                return;
            }
#endif

            bool active = sign == '+';

            p = skipspace(p);
            char pattern[BUFSIZE];
            if (*p == '(') {
                ptr *pbuf = NULL;
                ++p;
                char kind = evalp(&pbuf, &p);
                if (!REAL_ERROR && kind != TYPE_TXT)
                    error = NO_STRING_ERROR;
                if (REAL_ERROR) {
                    message_printf("#%s: ", ONPROMPT);
                    print_error(error);
                    ptrdel(&pbuf);
                    return;
                }
                if (pbuf != NULL) {
                    my_strncpy(pattern, ptrdata(pbuf), BUFSIZE - 1);
                    ptrdel(&pbuf);
                } else {
                    pattern[0] = '\0';
                }
                if (*p != '\0')
                    p = skipspace(p + 1);
                hastail = *p == '=';
                if (hastail)
                    ++p;
            } else {
                char *start = p;
                p = first_regular(start, '=');
                hastail = *p != '\0';
                if (hastail)
                    *p = '\0';
                my_strncpy(pattern, start, BUFSIZE - 1);

                if (hastail)
                    *p++ = '=';
            }

            if (*pattern == '\0') {
                wrap_printf("#error: pattern of #%ss must be non-empty.",
                            ONPROMPT);
                return;
            }

            void *regexp = NULL;
#ifdef USE_REGEXP
            if (type == ACTION_REGEXP && hastail) {
                char unesc_pat[BUFSIZE];

                /*
                 * HACK WARNING:
                 * we unescape regexp patterns now, instead of doing
                 * jit+unescape at runtime, as for weak actions.
                 */
                strcpy(unesc_pat, pattern);
                unescape(unesc_pat);

                regexp = malloc(sizeof(regex_t));
                if (regexp == NULL) {
                    out_of_memory();
                    return;
                }

#ifdef REG_UTF
                /* PCRE2: match UTF-8 characters, not bytes */
                const int cflags = REG_EXTENDED | REG_UTF;
#else
                const int cflags = REG_EXTENDED;
#endif
                int errcode = regcomp(regexp, unesc_pat, cflags);
                if (errcode != 0) {
                    int n = regerror(errcode, regexp, NULL, 0);
                    char *tmp = malloc(n);
                    if (tmp != NULL) {
                        if (regerror(errcode, regexp, tmp, n) == 0) {
                            errmsg("invalid regular expression");
                        } else {
                            message_printf("#regexp error: %s\n", tmp);
                        }
                        free(tmp);
                    } else {
                        error = NO_MEM_ERROR;
                        out_of_memory();
                    }
                    free(regexp);
                    return;
                }
            }
#endif
            char *command = p;

            if (hastail) {
                if (np != NULL && *np != NULL) {
                    change_actionorprompt(*np, pattern, command, type, regexp,
                                          onprompt);
                    (*np)->active = active;
                } else if (onprompt) {
                    add_new_prompt(label, pattern, command, active, type,
                                   regexp);
                } else {
                    add_new_action(label, pattern, command, active, type,
                                   regexp);
                }

                if (group != NULL) {
                    /* the space after the label was put back above, so the
                     * group name is still followed by the pattern */
                    p = first_regular(group, ' ');
                    *p = '\0';
                    np = (onprompt
                          ? lookup_prompt(label)
                          : lookup_action(label));
                    if (np == NULL || *np == NULL)
                        return; /* out of memory when adding it */
                    if ((*np)->group != NULL)
                        free((*np)->group);

                    if (*group == '\0' || strcmp(group, "*") == 0)
                        group = NULL;

                    (*np)->group = my_strdup(group);
                }
            }
        } else if (assign == '=') {
            /* '=': list action */
            if (np != NULL && *np != NULL) {
                fmt_next_edit("#%s %c%c%s %s=%s", ONPROMPT,
                              action_chars[(*np)->type],
                              (*np)->active ? '+' : '-',
                              (*np)->label,
                              (*np)->pattern,
                              (*np)->command);
            } else {
                wrap_printf("#no %s, cannot list label: \"%s\"", ONPROMPT,
                            label);
            }
        } else {
            /* '+', '-': turn action on/off */
            if (np != NULL && *np != NULL) {
                (*np)->active = sign == '+';
                if (opt_info) {
                    wrap_printf("#%s %c%s %s is now o%s.", ONPROMPT,
                                action_chars[(*np)->type], label,
                                (*np)->pattern, sign == '+' ? "n" : "ff");
                }
            } else {
                wrap_printf("#no %s, cannot turn o%s label: \"%s\"", ONPROMPT,
                            sign == '+' ? "n" : "ff", label);
            }
        }
    } else {
        /* anonymous action, cannot be regexp */

        if (onprompt) {
            message_printf("#anonymous prompts not supported.\n"
                           "#please use labelled prompts.\n");
            return;
        }

        char *command = first_regular(str, '=');

        if (*command == '=') {
            *command = '\0';

            char pattern[BUFSIZE];
            my_strncpy(pattern, str, BUFSIZE - 1);
            *command++ = '=';
            actionnode **np = lookup_action_pattern(pattern);
            if (*command != '\0') {
                if (np != NULL && *np != NULL)
                    change_actionorprompt(*np, NULL, command, ACTION_WEAK,
                                          NULL, false);
                else
                    add_anonymous_action(pattern, command, ACTION_WEAK, NULL);
            } else if (np != NULL && *np != NULL) {
                delete_action(np);
            } else {
                wrap_printf("#no action, cannot delete pattern: \"%s\"",
                            pattern);
                return;
            }
        } else {
            actionnode **np = lookup_action_pattern(str);
            if (np != NULL && *np != NULL) {
                fmt_next_edit("#action %s=%s",
                              (*np)->pattern,
                              (*np)->command);
            } else {
                wrap_printf("#no action, cannot show pattern: \"%s\"", str);
            }
        }
    }
}

#undef ONPROMPT

/*
 * display attribute syntax
 */
void show_attr_syntax(void)
{
    message_printf("#an attribute is: [<style>...] [<color>] [on <color>]\n"
                   "  styles:   bold, dim, italic, underline, blink, inverse,"
                   " hidden,\n"
                   "            strike, overline\n"
                   "  colors:   black, red, green, yellow, blue, magenta,"
                   " cyan, white,\n"
                   "            bright with a capital (Black, Red, ...), 0 to"
                   " 255 for the\n"
                   "            256-color palette, #rrggbb or none\n"
                   "  example:  bold yellow on blue\n");
}

/*
 * put escape sequences to turn on/off an attribute in given buffers,
 * which must have room for ATTR_SGR_MAX characters
 */
void attr_string(const struct attr *attr, char *begin, char *end)
{
    attr_sgr(attr, begin, ATTR_SGR_MAX);
    if (begin[0] != '\0')
        my_strncpy(end, tty_modenorm, ATTR_SGR_MAX - 1);
    else
        end[0] = '\0';
}

/*
 * return a static pointer to name of given attribute
 */
char *attr_name(const struct attr *attr)
{
    static char name[ATTR_NAME_MAX];
    attr_format(attr, name, sizeof(name));
    return name;
}

/*
 * show defined marks
 */
void show_marks(void)
{
    message_printf("#%s marker%s defined%c\n",
                   markers != NULL ? "the following" : "no",
                   (markers != NULL && markers->next == NULL
                    ? " is"
                    : "s are"),
                   markers != NULL ? ':' : '.');
    for (marknode *p = markers; p != NULL; p = p->next)
        tty_message("#mark %s%s=%s\n", p->b.mbeg ? "^" : "",
                    p->b.pattern, attr_name(&p->attr));
}

/*
 * parse arguments to the #mark command
 */
void parse_mark(char *str)
{
    if (*str == '=') {
        message_printf("#marker must be non-empty.\n");
        return;
    }
    char *p = first_regular(str, '=');
    bool mbeg = false;
    if (*p == '\0') {
        if (*str == '^') {
            mbeg = true;
            ++str;
        }
        unescape(str);
        marknode *n = *lookup_marker(str, mbeg);
        if (n != NULL) {
            ptr *pbuf = ptrmescape(NULL, n->b.pattern,
                                   strlen(n->b.pattern), false);
            if (MEM_ERROR) {
                ptrdel(&pbuf);
                return;
            }
            char *name = attr_name(&n->attr);
            fmt_next_edit("#mark %s%s=%s", n->b.mbeg ? "^" : "",
                          pbuf != NULL ? ptrdata(pbuf) : "", name);
            ptrdel(&pbuf);
        } else {
            wrap_printf("#unknown marker, cannot show: \"%s\"", str);
        }
    } else {
        *(p++) = '\0';
        p = skipspace(p);
        if (*str == '^') {
            mbeg = true;
            ++str;
        }
        char pattern[BUFSIZE];
        my_strncpy(pattern, str, BUFSIZE - 1);
        unescape(pattern);
        bool wild = false;
        for (char *p2 = pattern; *p2 != '\0'; ++p2) {
            if (ISMARKWILDCARD(*p2)) {
                wild = true;
                if (ISMARKWILDCARD(*(p2 + 1))) {
                    error = SYNTAX_ERROR;
                    wrap_printf("#error: two wildcards (& or $) may not be"
                                " next to eachother");
                    return;
                }
            }
        }

        marknode **np = lookup_marker(pattern, mbeg);
        struct attr attr;
        char why[BUFSIZE];
        if (!attr_parse_why(p, &attr, why, sizeof(why))) {
            message_printf("#mark: %s.\n", why);
            error = SYNTAX_ERROR;
            if (opt_info)
                show_attr_syntax();
        } else if (*p == '\0') {
            marknode *n = *np;
            if (n != NULL) {
                if (opt_info) {
                    message_printf("#deleting mark: %s%s=%s\n",
                                   n->b.mbeg ? "^" : "",
                                   n->b.pattern, attr_name(&n->attr));
                }
                delete_marknode(np);
            } else {
                wrap_printf("#unknown marker, cannot delete: \"%s%s\"",
                            mbeg ? "^" : "", pattern);
            }
        } else {
            if (*np != NULL) {
                (*np)->attr = attr;
                if (opt_info) {
                    message_printf("#changed");
                }
            } else {
                add_marknode(pattern, &attr, mbeg, wild);
                if (opt_info) {
                    message_printf("#new");
                }
            }
            if (opt_info)
                tty_message(" mark: %s%s=%s\n", mbeg ? "^" : "",
                            pattern, attr_name(&attr));
        }
    }
}

/*
 * show defined substitutions
 */
void show_substitutions(void)
{
    message_printf("#%s substitution%s defined%c\n",
                   substitutions != NULL ? "the following" : "no",
                   (substitutions != NULL && substitutions->next == NULL
                    ? " is"
                    : "s are"),
                   substitutions != NULL ? ':' : '.');

    ptr *pbuf = NULL;
    for (substnode *p = substitutions; p != NULL; p = p->next) {
        pbuf = ptrsubstdef(pbuf, p->b.mbeg, p->b.pattern, p->replacement);
        if (MEM_ERROR)
            break;
        tty_message("#substitute %s\n", ptrdata(pbuf));
    }
    ptrdel(&pbuf);
}

/*
 * tell the user what happened to a substitution
 */
static void subst_info(const char *what, bool mbeg, char *pattern,
                       char *replacement)
{
    if (!opt_info)
        return;

    ptr *pbuf = ptrsubstdef(NULL, mbeg, pattern, replacement);
    if (!MEM_ERROR)
        message_printf("#%s substitution: %s\n", what, ptrdata(pbuf));
    ptrdel(&pbuf);
}

/*
 * parse arguments to the #substitute command
 */
void parse_substitute(char *str)
{
    if (*str == '=') {
        message_printf("#substitute must be non-empty.\n");
        return;
    }

    char *p = first_regular(str, '=');
    bool mbeg = false;
    if (*str == '^') {
        mbeg = true;
        ++str;
    }

    if (*p == '\0') {
        unescape(str);
        substnode *n = *lookup_subst(str, mbeg);
        if (n == NULL) {
            wrap_printf("#unknown substitution, cannot show: \"%s\"", str);
            return;
        }
        ptr *pbuf = ptrsubstdef(NULL, n->b.mbeg, n->b.pattern,
                                n->replacement);
        if (!MEM_ERROR)
            fmt_next_edit("#substitute %s", ptrdata(pbuf));
        ptrdel(&pbuf);
        return;
    }

    *p = '\0';
    ++p;

    char pattern[BUFSIZE];
    my_strncpy(pattern, str, BUFSIZE - 1);
    unescape(pattern);
    if (*pattern == '\0') {
        message_printf("#substitute must be non-empty.\n");
        return;
    }

    bool wild = false;
    for (const char *q = pattern; *q != '\0'; ++q) {
        if (!ISMARKWILDCARD(*q))
            continue;
        wild = true;
        if (ISMARKWILDCARD(q[1])) {
            error = SYNTAX_ERROR;
            wrap_printf("#error: two wildcards (& or $) may not be next to"
                        " eachother");
            return;
        }
    }

    substnode **np = lookup_subst(pattern, mbeg);
    substnode *n = *np;
    unescape(p);
    if (*p == '\0') {
        if (n == NULL) {
            wrap_printf("#unknown substitution, cannot delete: \"%s%s\"",
                        mbeg ? "^" : "", pattern);
            return;
        }
        subst_info("deleting", n->b.mbeg, n->b.pattern, n->replacement);
        delete_substnode(np);
    } else if (n != NULL) {
        char *replacement = my_strdup(p);
        if (replacement == NULL) {
            out_of_memory();
            return;
        }
        free(n->replacement);
        n->replacement = replacement;
        subst_info("changed", mbeg, pattern, p);
    } else {
        add_substnode(pattern, p, mbeg, wild);
        subst_info("new", mbeg, pattern, p);
    }
}

/*
 * turn ASCII description of a sequence
 * into raw escape sequence
 * return pointer to end of ASCII description
 */
static char *unescape_seq(char *buf, char *seq, int *seqlen)
{
    char *start = buf;
    enum { NORM, ESCSINGLE, ESCAPE, CARET, DONE } state = NORM;

    for (; *seq != '\0'; ++seq) {
        char c = *seq;
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
            if (c >= 'A' && c <= '_') {
                *(buf++) = c & 0x1f;
            } else if (c == '?') {
                /* special case: ^? == DEL */
                *(buf++) = ASCII_DEL;
            }
            state = NORM;
            break;
        case ESCSINGLE:
        case ESCAPE:
            /*
             * \012 ==> octal number
             */
            if (state == ESCSINGLE && is_odigit(seq[0])
                && is_odigit(seq[1]) && is_odigit(seq[2])) {
                *(buf++) = (((seq[0] - '0') << 6)
                            | ((seq[1] - '0') << 3)
                            | (seq[2] - '0'));
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
static int get_one_char(int timeout)
{
    /* tty_read() may hold characters that select() cannot see */
    bool buffered = tty_has_chars();

 again:
    if (!buffered) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(tty_read_fd, &fds);
        struct timeval timeoutbuf = {
            .tv_sec = timeout / mSEC_PER_SEC,
            .tv_usec = (timeout % mSEC_PER_SEC) * uSEC_PER_mSEC,
        };
        int n = select(tty_read_fd + 1, &fds, NULL, NULL,
                       timeout != 0 ? &timeoutbuf : NULL);
        if (n == 0 || (n == -1 && errno == EINTR))
            return -1;
        if (n == -1) {
            errmsg("cannot wait for the terminal");
            return -1;
        }
    }
    int n;
    char c;
    do {
        n = tty_read(&c, 1);
    } while (n < 0 && errno == EINTR);
    if (n == 1)
        return (unsigned char)c;
    if (n < 0) {
        if (errno != EAGAIN)
            errmsg("cannot read from the terminal");
        return -1;
    }
    /* n == 0: at most an incomplete character is buffered, wait for more */
    if (timeout == 0 || buffered) {
        buffered = false;
        goto again;
    }
    return -1;
}

/*
 * print an escape sequence in human-readably form.
 */
static void print_seq(char *seq, int len)
{
    char buf[6 * CAPLEN + 1];
    int n = 0;
    for (; len > 0 && n < (int)sizeof(buf) - 6; --len) {
        unsigned char ch = *(seq++);
        if (ch == ASCII_ESC) {
            n += sprintf(buf + n, "esc ");
            continue;
        }
        if (ch < ' ') {
            buf[n++] = '^';
            ch |= '@';
        }
        if (ch == ' ')
            n += sprintf(buf + n, "space ");
        else if (ch == ASCII_DEL)
            n += sprintf(buf + n, "del ");
        else if (ch > ASCII_MAX)
            n += sprintf(buf + n, "\\%03o ", ch);
        else
            n += sprintf(buf + n, "%c ", ch);
    }
    buf[n] = '\0';
    tty_message("%s", buf);
}

/*
 * return a static pointer to escape sequence made printable, for use in
 * definition-files
 */
char *seq_name(char *seq, int len)
{
    static char buf[CAPLEN * 4];
    char *p = buf;
    /*
     * rules: control chars are written as ^X, where
     * X is char | 64
     *
     * codes > 0x80  ==>  octal \012
     *
     * special case: DEL is written ^?
     */
    while (len-- != 0) {
        unsigned char c = *seq++;
        if (c == '^' || (c != 0 && strchr(SPECIAL_CHARS, c) != NULL))
            *(p++) = ESC;

        if (c < ' ') {
            *(p++) = '^';
            *(p++) = c | '@';
        } else if (c == ASCII_DEL) {
            *(p++) = '^';
            *(p++) = '?';
        } else if (c > ASCII_MAX) {
            sprintf(p, "\\%03o", c);
            p += strlen(p);
        } else {
            *(p++) = c;
        }
    }
    *p = '\0';
    return buf;
}

/*
 * read a single escape sequence from the keyboard
 * prompting user for it; return static pointer
 */
static char *read_seq(char *name, int *len)
{
    static char seq[CAPLEN];

    message_printf("#please press the key \"%s\" : ", name);
    tty_flush();

    int tmp = get_one_char(0);
    if (tmp < 0) {
        tty_message("#unable to get key. Giving up.\n");
        return NULL;
    }
    seq[0] = tmp;

    int i = 1;
    while (i < CAPLEN - 1 && (tmp = get_one_char(KBD_TIMEOUT)) >= 0)
        seq[i++] = tmp;
    *len = i;
    print_seq(seq, i);

    tty_putc('\n');
    if (seq[0] >= ' ' && seq[0] <= '~') {
        message_printf("#that is not a redefinable key.\n");
        return NULL;
    }
    return seq;
}

/*
 * show full definition of one binding,
 * with custom message
 */
static void show_single_bind(const char *msg, keynode *p)
{
    if (p->funct == key_run_command) {
        message_printf("#%s %s %s=%s\n", msg, p->name,
                       seq_name(p->sequence, p->seqlen), p->call_data);
    } else {
        message_printf("#%s %s %s=%s%s%s\n", msg, p->name,
                       seq_name(p->sequence, p->seqlen),
                       internal_functions[lookup_edit_function(p->funct)].name,
                       p->call_data != NULL ? " " : "",
                       p->call_data != NULL ? p->call_data : "");
    }
}

/*
 * list the key bindings whose sequences start with the 'len' bytes of
 * 'prefix', the keys named 'names', for a key sequence followed by C-h
 */
void show_binds_starting(const char *prefix, int len, const char *names)
{
    message_printf("#key bindings starting with %s:\n", names);
    for (keynode *p = keydefs; p != NULL; p = p->next) {
        if (p->seqlen > len && memcmp(p->sequence, prefix, len) == 0)
            show_single_bind("bind", p);
    }
}

/*
 * list all the editing functions by group, with their descriptions and the
 * keys bound to them
 */
void show_edit_functions(void)
{
    enum { FUNCTION_COLUMN = 24 };
    message_printf("#editing functions:\n");
    for (const edit_function *f = internal_functions + 1; f->name != NULL;
         ++f) {
        if (f == internal_functions + 1 || strcmp(f->group, f[-1].group) != 0)
            message_printf("%s#%s:\n", f == internal_functions + 1 ? "" : "\n",
                           f->group);
        char keys[BUFSIZE];
        int n = 0;
        for (const keynode *k = keydefs; k != NULL; k = k->next) {
            if (k->funct == f->funct && n < (int)sizeof(keys))
                n += snprintf(keys + n, sizeof(keys) - n, "%s%s",
                              n == 0 ? " (" : ", ", k->name);
        }
        message_printf("%-*s%s%s%s\n", FUNCTION_COLUMN, f->name, f->help,
                       n > 0 ? keys : "", n > 0 ? ")" : "");
    }
}

/*
 * reset the key 'name' to its default binding, or delete it if it has
 * none
 */
void reset_bind(char *name)
{
    const default_bind *d = tty_default_bind(name);
    keynode **kp = lookup_key(name);
    if (d == NULL) {
        if (*kp == NULL) {
            wrap_printf("#no key named \"%s\", and no default for it.", name);
        } else {
            show_single_bind("deleting key binding:", *kp);
            delete_keynode(kp);
        }
        return;
    }
    if (*kp != NULL)
        delete_keynode(kp);
    /* another key with the same sequence would hide this one */
    int len = strlen(d->seq);
    for (kp = &keydefs; *kp != NULL; ) {
        if ((*kp)->seqlen == len
            && memcmp((*kp)->sequence, d->seq, len) == 0) {
            show_single_bind("deleting key binding:", *kp);
            delete_keynode(kp);
        } else {
            kp = &(*kp)->next;
        }
    }
    add_keynode(name, d->seq, 0, d->funct, d->command);
    kp = lookup_key(name);
    if (*kp != NULL)
        show_single_bind("default key binding:", *kp);
}

/*
 * list keyboard bindings
 */
void show_binds(bool edit)
{
    int count = 0;
    for (keynode *p = keydefs; p != NULL; p = p->next) {
        if (edit != (p->funct == key_run_command)) {
            if (count == 0) {
                if (edit) {
                    message_printf("#line-editing keys:\n");
                } else {
                    message_printf("#user-defined keys:\n");
                }
            }
            show_single_bind("bind", p);
            ++count;
        }
    }
    if (count == 0) {
        message_printf("#no key bindings defined right now.\n");
    }
}

/*
 * interactively create a new keybinding
 */
static void define_new_key(char *name, char *command)
{
    int seqlen;
    char *seq = read_seq(name, &seqlen);
    if (seq == NULL)
        return;

    for (keynode *p = keydefs; p != NULL; p = p->next) {
        /* don't allow binding of supersets of another bind */
        if (memcmp(p->sequence, seq, MIN2(p->seqlen, seqlen)) == 0) {
            show_single_bind("key already bound as:", p);
            return;
        }
    }

    char *arg;
    int function = lookup_edit_name(command, &arg);
    if (function != 0)
        add_keynode(name, seq, seqlen,
                    internal_functions[function].funct, arg);
    else
        add_keynode(name, seq, seqlen, key_run_command, command);

    if (opt_info) {
        message_printf("#new key binding: %s %s=%s\n",
                       name, seq_name(seq, seqlen), command);
    }
}

/*
 * parse the #bind command non-interactively.
 */
static void parse_bind_noninteractive(char *arg)
{
    char *p = strchr(arg, ' ');
    if (p == NULL) {
        message_printf("#syntax error: \"#bind %s\"\n", arg);
        return;
    }
    *(p++) = '\0';
    p = skipspace(p);
    char *seq = p;

    char rawseq[CAPLEN];
    int seqlen;
    p = unescape_seq(rawseq, p, &seqlen);
    if (p[0] == '\0' || p[1] == '\0') {
        message_printf("#syntax error: \"#bind %s %s\"\n", arg, seq);
        return;
    }
    *p++ = '\0';

    keynode **kp = lookup_key(arg);
    bool redefined = kp != NULL && *kp != NULL;
    if (redefined)
        delete_keynode(kp);

    char *params;
    int function = lookup_edit_name(p, &params);
    if (function != 0)
        add_keynode(arg, rawseq, seqlen,
                    internal_functions[function].funct, params);
    else
        add_keynode(arg, rawseq, seqlen, key_run_command, p);

    if (opt_info) {
        message_printf("#%s: %s %s=%s\n",
                       redefined ? "redefined key" : "new key binding",
                       arg, seq, p);
    }
}

/*
 * parse the argument of the #bind command (interactive)
 */
void parse_bind(char *arg)
{
    char *name = arg;
    char *p = first_valid(arg, '=');
    char *q = skipspace(first_valid(arg, ' '));

    if (*p != '\0' && *q != '\0' && p > q) {
        parse_bind_noninteractive(arg);
        return;
    }

    if (*p != '\0') {
        *(p++) = '\0';
        keynode **npp = lookup_key(name);
        keynode *np = *npp;
        if (*p != '\0') {
            char *command = p;
            if (np != NULL) {
                free(np->call_data);
                char *params;
                int function = lookup_edit_name(command, &params);
                if (function != 0) {
                    np->call_data = my_strdup(params);
                    np->funct = internal_functions[function].funct;
                } else {
                    np->call_data = my_strdup(command);
                    np->funct = key_run_command;
                }
                if (opt_info) {
                    message_printf("#redefined key: %s %s=%s\n", name,
                                   seq_name(np->sequence, np->seqlen),
                                   command);
                }
            } else {
                define_new_key(name, command);
            }
        } else if (np != NULL) {
            if (opt_info)
                show_single_bind("deleting key binding:", np);
            delete_keynode(npp);
        } else {
            wrap_printf("#no such key: \"%s\"", name);
        }
    } else {
        keynode *np = *lookup_key(name);
        if (np != NULL) {
            char *seqname = seq_name(np->sequence, np->seqlen);

            if (np->funct == key_run_command) {
                fmt_next_edit("#bind %s %s=%s", name, seqname, np->call_data);
            } else {
                const char *fname
                    = internal_functions[lookup_edit_function(np->funct)].name;
                fmt_next_edit("#bind %s %s=%s%s%s",
                              name, seqname, fname,
                              np->call_data != NULL ? " " : "",
                              np->call_data != NULL ? np->call_data : "");
            }
        } else {
            wrap_printf("#no such key: \"%s\"", name);
        }
    }
}

void parse_rebind(char *arg)
{
    arg = skipspace(arg);
    if (*arg == '\0') {
        message_printf("#rebind: missing key.\n");
        return;
    }

    char *seq = first_valid(arg, ' ');
    if (*seq != '\0') {
        *seq++ = '\0';
        seq = skipspace(seq);
    }

    keynode **kp = lookup_key(arg);
    if (kp == NULL || *kp == NULL) {
        wrap_printf("#no such key: \"%s\"", arg);
        return;
    }

    char rawseq[CAPLEN];
    int seqlen;
    if (*seq == '\0') {
        seq = read_seq(arg, &seqlen);
        if (seq == NULL)
            return;
    } else {
        unescape_seq(rawseq, seq, &seqlen);
        seq = rawseq;
    }

    if (seqlen == 0) {
        message_printf("#rebind: empty key sequence.\n");
        return;
    }

    for (keynode *p = keydefs; p != NULL; p = p->next) {
        if (p == *kp)
            continue;
        if (memcmp(p->sequence, seq, MIN2(seqlen, p->seqlen)) == 0) {
            show_single_bind("key already bound as:", p);
            return;
        }
    }

    char *new_seq = malloc(seqlen);
    if (new_seq == NULL) {
        out_of_memory();
        return;
    }
    memmove(new_seq, seq, seqlen);

    char **old = &(*kp)->sequence;
    if (*old != NULL)
        free(*old);
    *old = new_seq;
    (*kp)->seqlen = seqlen;

    if (opt_info)
        show_single_bind("redefined key:", *kp);
}

/*
 * evaluate an expression, or unescape a text.
 * set value of start and end line if <(expression...) or !(expression...)
 * if needed, use/malloc "pbuf" as buffer (on error, also free pbuf)
 * return resulting char *
 */
char *redirect(char *arg, ptr **pbuf, char *kind, const char *name,
               bool also_num, long *start, long *end)
{
    if (pbuf == NULL) {
        error = INTERNAL_ERROR;
        print_error(error);
        return NULL;
    }

    char *tmp = skipspace(arg);
    char k = *tmp;
    if (k == '!' || k == '<') {
        ++tmp;
        arg = tmp;
    } else {
        k = '\0';
    }

    *start = 0;
    *end = 0;

    if (*tmp == '(') {
        arg = tmp + 1;
        int type = evalp(pbuf, &arg);
        if (!REAL_ERROR && type != TYPE_TXT && !also_num)
            error = NO_STRING_ERROR;
        if (REAL_ERROR) {
            message_printf("#%s: ", name);
            print_error(error);
            ptrdel(pbuf);
            return NULL;
        }
        for (int i = 0; i < 2; ++i) {
            if (*arg == CMDSEP) {
                ++arg;
                if (i == 0 && *arg == CMDSEP) {
                    *start = 1;
                    continue;
                } else if (i != 0 && *arg == ')') {
                    *end = LONG_MAX;
                    continue;
                }

                long buf;
                type = evall(&buf, &arg);
                if (!REAL_ERROR && type != TYPE_NUM)
                    error = NO_NUM_VALUE_ERROR;
                if (REAL_ERROR) {
                    message_printf("#%s: ", name);
                    print_error(error);
                    ptrdel(pbuf);
                    return NULL;
                }
                if (i != 0)
                    *end = buf;
                else
                    *start = buf;
            }
        }
        if (*arg != ')') {
            message_printf("#%s: ", name);
            error = MISSING_PAREN_ERROR;
            print_error(error);
            ptrdel(pbuf);
            return NULL;
        }
        if (*pbuf == NULL) {
            /* make space to add a final \n */
            *pbuf = ptrsetlen(*pbuf, 1);
            ptrzero(*pbuf);
            if (REAL_ERROR) {
                print_error(error);
                ptrdel(pbuf);
                return NULL;
            }
        }
        arg = ptrdata(*pbuf);
        if (*start == 0 && *end != 0)
            *start = 1;
    } else {
        unescape(arg);
    }

    *kind = k;
    return arg;
}

void show_vars(void)
{
    ptr *p = NULL;

    message_printf("#the following variables are defined:\n");

    for (enum var_kind kind = VAR_NUM; !REAL_ERROR && kind < VAR_KINDS;
         ++kind) {
        for (varnode *v = named_vars[kind].sorted; v != NULL; v = v->snext) {
            if (kind == VAR_NUM) {
                tty_message("#(@%s = %ld)\n", v->name, v->num);
            } else {
                p = ptrescape(p, v->str, false);
                if (REAL_ERROR) {
                    print_error(error);
                    break;
                }
                tty_message("#($%s = \"%s\")\n", v->name,
                            p != NULL ? ptrdata(p) : "");
            }
        }
    }
    for (int i = -GLOBAL_VAR_COUNT; !REAL_ERROR && i < PARAM_COUNT; ++i) {
        if (*num_var(i) != 0)
            tty_message("#(@%d = %ld)\n", i, *num_var(i));
    }
    for (int i = -GLOBAL_VAR_COUNT; !REAL_ERROR && i < PARAM_COUNT; ++i) {
        if (*text_var(i) != NULL && ptrlen(*text_var(i)) != 0) {
            p = ptrescape(p, *text_var(i), false);
            if (p != NULL && ptrlen(p) != 0)
                tty_message("#($%d = \"%s\")\n", i, ptrdata(p));
        }
    }
    ptrdel(&p);
}

void show_delaynode(delaynode *p, int in_or_at)
{
    update_now();
    long d = diff_vtime(&p->when, &now);
    struct tm *s = localtime(&p->when.tv_sec);
    if (in_or_at != 0) {
        if (in_or_at == 2) {
            char buf[BUFSIZE];
            strftime(buf, BUFSIZE - 1, "%H%M%S", s);
            fmt_next_edit("#at %s (%s) %s", p->name, buf, p->command);
        } else {
            fmt_next_edit("#in %s (%ld) %s", p->name, d, p->command);
        }
    } else {
        char buf[BUFSIZE];
        strftime(buf, BUFSIZE - 1, "%H:%M:%S", s);
        message_printf("#at (%s) #in (%ld) \"%s\" %s\n", buf, d, p->name,
                       p->command);
    }
}

void show_delays(void)
{
    int n = ((delays == NULL ? 0 : delays->next == NULL ? 1 : 2)
             + (dead_delays == NULL ? 0 : dead_delays->next == NULL ? 1 : 2));

    message_printf("#%s delay label%s defined%c\n",
                   n != 0 ? "the following" : "no",
                   n == 1 ? " is" : "s are", n != 0 ? ':' : '.');
    for (delaynode *p = delays; p != NULL; p = p->next)
        show_delaynode(p, 0);
    for (delaynode *p = dead_delays; p != NULL; p = p->next)
        show_delaynode(p, 0);
}

void change_delaynode(delaynode **p, char *command, long millisec)
{
    delaynode *m = *p;

    *p = m->next;
    m->when.tv_usec = (millisec % mSEC_PER_SEC) * uSEC_PER_mSEC;
    m->when.tv_sec = millisec / mSEC_PER_SEC;
    update_now();
    add_vtime(&m->when, &now);
    if (*command != '\0') {
        if (strlen(command) > strlen(m->command)) {
            free(m->command);
            m->command = my_strdup(command);
        } else {
            strcpy(m->command, command);
        }
    }
    insert_delaynode(m, millisec < 0);
    if (opt_info) {
        message_printf("#changed ");
        show_delaynode(m, 0);
    }
}

void new_delaynode(char *name, char *command, long millisec)
{
    vtime t = {
        .tv_sec = millisec / mSEC_PER_SEC,
        .tv_usec = (millisec % mSEC_PER_SEC) * uSEC_PER_mSEC,
    };
    update_now();
    add_vtime(&t, &now);
    delaynode *node = add_delaynode(name, command, &t, millisec < 0);
    if (opt_info && node != NULL) {
        message_printf("#new ");
        show_delaynode(node, 0);
    }
}

void show_history(int count)
{
    if (count == 0)
        count = lines - 1;
    if (count >= MAX_HIST)
        count = MAX_HIST - 1;
    int i = curline - count;
    if (i < 0)
        i += MAX_HIST;

    while (count != 0) {
        if (hist[i] != NULL) {
            message_printf("#%2d: %s\n", count, hist[i]);
        }
        --count;
        ++i;
        if (i == MAX_HIST)
            i = 0;
    }
}

void exe_history(int count)
{
    if (count >= MAX_HIST)
        count = MAX_HIST - 1;
    int i = curline - count;
    if (i < 0)
        i += MAX_HIST;
    if (hist[i] != NULL) {
        char buf[BUFSIZE];
        strcpy(buf, hist[i]);
        parse_user_input(buf, false);
    }
}
