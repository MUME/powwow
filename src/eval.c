/*
 *  eval.c  --  functions for builtin calculator
 *
 *  (created: Massimiliano Ghilardi (Cosmos), Jan 15th, 1995)
 *
 *  Copyright (C) 1998 by Massimiliano Ghilardi
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 */

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>

#include <sys/time.h>
#include <sys/types.h>

#include "defines.h"
#include "main.h"
#include "utils.h"
#include "cmd2.h"
#include "list.h"
#include "map.h"
#include "tty.h"
#include "edit.h"
#include "eval.h"
#include "charset.h"

typedef struct {
  int  type;
  long num;      /* used for numeric types or as index for all variables */
  ptr  *txt;      /* used for text types */
} object;

#define LEFT  1
#define RIGHT 2

#define BINARY     0
#define PRE_UNARY  LEFT
#define POST_UNARY RIGHT

#define LOWEST_UNARY_CODE 49
/*
 *  it would be 47, but operators 47 '(' and 48 ')'
 *  are treated separately
 */

enum op_codes {
  op_null=0, op_comma, op_eq,
  op_or_or_eq, op_xor_xor_eq, op_and_and_eq, op_or_eq, op_xor_eq, op_and_eq,
  op_lshift_eq, op_rshift_eq, op_plus_eq, op_minus_eq, op_times_eq,
  op_div_eq, op_ampersand_eq,
  op_or_or, op_xor_xor, op_and_and, op_or, op_xor, op_and,
  op_less, op_less_eq, op_greater, op_greater_eq, op_eq_eq, op_not_eq,
  op_lshift, op_rshift, op_plus, op_minus, op_times, op_division, op_ampersand,

  op_colon_less, op_colon_greater, op_less_colon, op_greater_colon,
  op_point_less, op_point_greater, op_less_point, op_greater_point,
  op_colon, op_point, op_question, op_another_null,

  op_left_paren, op_right_paren, op_not, op_tilde,
  op_pre_plus_plus, op_post_plus_plus, op_pre_minus_minus, op_post_minus_minus,
  op_star, op_print, op_random, op_attr, op_colon_question, op_point_question,
  op_pre_plus, op_pre_minus, op_a_circle, op_dollar, op_pre_null, op_post_null
};

typedef enum op_codes operator;

typedef struct {
    char priority, assoc, syntax;
    const char *name;
    operator code;
} operator_list;

typedef struct {
  object obj[MAX_STACK];
  int curr_obj;
  operator op[MAX_STACK];
  int curr_op;
} stack;

const char *const error_msg[] = {
    "unknown error",
    "math stack overflow",
    "math stack underflow",
    "stack overflow",
    "stack underflow",
    "expression syntax",
    "operator expected",
    "value expected",
    "division by zero",
    "operand or index out of range",
    "missing right parenthesis",
    "missing left parenthesis",
    "internal error!",
    "operator not supported",
    "operation not completed (internal error)",
    "out of memory",
    "text/string longer than limit, discarded",
    "infinite loop",
    "numeric value expected",
    "string expected",
    "missing label",
    "missing separator \";\"",
    "#history recursion too deep",
    "user break",
    "too many defined variables",
    "undefined variable",
    "invalid digit in numeric value",
    "bad attribute syntax",
    "invalid variable name",
};

static const operator_list op_list[] = {
    { 0, 0,     0,          "",     op_null             },

    { 1, LEFT,  BINARY,     ",",    op_comma            },

    { 2, RIGHT, BINARY,     "=",    op_eq               },
    { 2, RIGHT, BINARY,     "||=",  op_or_or_eq         },
    { 2, RIGHT, BINARY,     "^^=",  op_xor_xor_eq       },
    { 2, RIGHT, BINARY,     "&&=",  op_and_and_eq       },
    { 2, RIGHT, BINARY,     "|=",   op_or_eq            },
    { 2, RIGHT, BINARY,     "^=",   op_xor_eq           },
    { 2, RIGHT, BINARY,     "&=",   op_and_eq           },
    { 2, RIGHT, BINARY,     "<<=",  op_lshift_eq        },
    { 2, RIGHT, BINARY,     ">>=",  op_rshift_eq        },
    { 2, RIGHT, BINARY,     "+=",   op_plus_eq          },
    { 2, RIGHT, BINARY,     "-=",   op_minus_eq         },
    { 2, RIGHT, BINARY,     "*=",   op_times_eq         },
    { 2, RIGHT, BINARY,     "/=",   op_div_eq           },
    { 2, RIGHT, BINARY,     "%=",   op_ampersand_eq     },

    { 3, LEFT,  BINARY,     "||",   op_or_or            },

    { 4, LEFT,  BINARY,     "^^",   op_xor_xor          },

    { 5, LEFT,  BINARY,     "&&",   op_and_and          },

    { 6, LEFT,  BINARY,     "|",    op_or               },

    { 7, LEFT,  BINARY,     "^",    op_xor              },

    { 8, LEFT,  BINARY,     "&",    op_and              },

    { 9, LEFT,  BINARY,     "<",    op_less             },
    { 9, LEFT,  BINARY,     "<=",   op_less_eq          },
    { 9, LEFT,  BINARY,     ">",    op_greater          },
    { 9, LEFT,  BINARY,     ">=",   op_greater_eq       },
    { 9, LEFT,  BINARY,     "==",   op_eq_eq            },
    { 9, LEFT,  BINARY,     "!=",   op_not_eq           },

    {10, LEFT,  BINARY,     "<<",   op_lshift           },
    {10, LEFT,  BINARY,     ">>",   op_rshift           },

    {11, LEFT,  BINARY,     "+",    op_plus             },
    {11, LEFT,  BINARY,     "-",    op_minus            },

    {12, LEFT,  BINARY,     "*",    op_times            },
    {12, LEFT,  BINARY,     "/",    op_division         },
    {12, LEFT,  BINARY,     "%",    op_ampersand        },

    {14, LEFT,  BINARY,     ":<",   op_colon_less       },
    {14, LEFT,  BINARY,     ":>",   op_colon_greater    },
    {14, LEFT,  BINARY,     "<:",   op_less_colon       },
    {14, LEFT,  BINARY,     ">:",   op_greater_colon    },
    {14, LEFT,  BINARY,     ".<",   op_point_less       },
    {14, LEFT,  BINARY,     ".>",   op_point_greater    },
    {14, LEFT,  BINARY,     "<.",   op_less_point       },
    {14, LEFT,  BINARY,     ">.",   op_greater_point    },
    {14, LEFT,  BINARY,     ":",    op_colon            },
    {14, LEFT,  BINARY,     ".",    op_point            },
    {14, LEFT,  BINARY,     "?",    op_question         },

    { 0, 0,     0,          "",     op_another_null     },

    { 0, RIGHT, PRE_UNARY,  "(",    op_left_paren       },
    { 0, RIGHT, POST_UNARY, ")",    op_right_paren      },

    {13, RIGHT, PRE_UNARY,  "!",    op_not              },
    {13, RIGHT, PRE_UNARY,  "~",    op_tilde            },
    {13, RIGHT, PRE_UNARY,  "++",   op_pre_plus_plus    },
    {13, RIGHT, POST_UNARY, "++",   op_post_plus_plus   },
    {13, RIGHT, PRE_UNARY,  "--",   op_pre_minus_minus  },
    {13, RIGHT, POST_UNARY, "--",   op_post_minus_minus },
    {13, RIGHT, PRE_UNARY,  "*",    op_star             },
    {13, RIGHT, PRE_UNARY,  "%",    op_print            },
    {13, RIGHT, PRE_UNARY,  "rand", op_random           },
    {13, RIGHT, PRE_UNARY,  "attr", op_attr             },

    {14, LEFT,  PRE_UNARY,  ":?",   op_colon_question   },
    {14, LEFT,  PRE_UNARY,  ".?",   op_point_question   },

    {15, RIGHT, PRE_UNARY,  "+",    op_pre_plus         },
    {15, RIGHT, PRE_UNARY,  "-",    op_pre_minus        },
    {15, RIGHT, PRE_UNARY,  "@",    op_a_circle         },
    {15, RIGHT, PRE_UNARY,  "$",    op_dollar           },

    { 0, 0,     PRE_UNARY,  "",     op_pre_null         },
    { 0, 0,     POST_UNARY, "",     op_post_null        }
};

static stack stk;
static char *line;
static int depth;
int error;

/* return a uniformly distributed random number from 0 to n - 1 */
static long random_below(long n)
{
    uint64_t un = (uint64_t)n;
    /*
     * skip the 2^64 % n lowest values, so that every remainder is equally
     * likely
     */
    uint64_t min = -un % un;
    uint64_t r;
    do {
        /* lrand48() gives 31 random bits */
        r = (((uint64_t)lrand48() << 33)
             | ((uint64_t)lrand48() << 2)
             | ((uint64_t)lrand48() & 3));
    } while (r < min);
    return (long)(r % un);
}

void print_error(int err_num)
{
    clear_input_line(1);
    if (error == NO_MEM_ERROR) {
	tty_message("#out of memory\n");
    } else
	tty_message("#error: %s.\n", error_msg[err_num]);
}

static int push_op(operator *op)
{
    if (stk.curr_op<MAX_STACK) {
	stk.op[++stk.curr_op]=*op;
	return 1;
    }
    else {
	error=STACK_OV_ERROR;
	return 0;
    }
}

static int pop_op(operator *op)
{
    if (stk.curr_op>=0) {
	*op=stk.op[stk.curr_op--];
	return 1;
    }
    else {
	error=STACK_UND_ERROR;
	return 0;
    }
}

static int push_obj(object *obj)
{
    object *tmp;

    int curr=stk.curr_obj;

    if (curr<MAX_STACK) {
	tmp = stk.obj + (stk.curr_obj = ++curr);
	memmove(tmp, obj, sizeof(object));
	return 1;
    }
    else {
	error=STACK_OV_ERROR;
	return 0;
    }
}

static int pop_obj(object *obj)
{
    object *tmp;

    int curr=stk.curr_obj;

    if (curr>=0) {
	tmp = stk.obj + curr;
	stk.curr_obj--;
	memmove(obj, tmp, sizeof(object));
	return 1;
    }
    else {
	error=STACK_UND_ERROR;
	return 0;
    }
}

static int check_operator(char side, operator *op, int mindepth)
{
    int i, max, len;
    operator match;
    const char *name;
    char c, d;

    if (!(c=*line) || c == CMDSEP) {
	*op = side==BINARY ? op_null : side==LEFT ? op_pre_null : op_post_null;
	return 1;
    }
    else if ((c=='$' || c=='@') && (d=line[1]) && (isalpha(d) || d=='_'))
	return 0;           /* Danger! found named variable */

    else if (side==LEFT && c=='(') {
	line++;
	depth++;
	*op=op_left_paren;
	return 1;
    }
    else if (side==RIGHT && c==')') {
	if (--depth >= mindepth) {
	    line++;
	    *op=op_right_paren;
	}
	else 		   /* exit without touching the parenthesis */
	    *op=op_post_null;
	return 1;
    }
    else if (side==RIGHT && (c=='}' || c==']') && depth == mindepth) {
	/* allow also exiting with a '}' or a ']' */
	--depth;
	*op=op_post_null;
	return 1;
    }

    for (max=match=0, i=(side==BINARY ? 1 : LOWEST_UNARY_CODE);
	*(name = op_list[i].name); i++)
	if ((len=strlen(name)) > max &&
	    (side==BINARY || side==op_list[i].syntax) &&
	    !strncmp(line, name, (size_t)len)) {
	    match=op_list[i].code;
	    max=len;
	}

    if (match) {
	*op=match;
	line+=max;
	return 1;
    }
    else {
	*op= side==BINARY ? op_null : side==PRE_UNARY ? op_pre_null : op_post_null;
	if (side==BINARY)
	    error=NO_OPERATOR_ERROR;
    }
    return 0;
}

static int check_object(object *obj)
{
    long i=0, base = 10;
    char c, *end, digit;

    if (c=*line, c == '#' || isdigit(c)) {
	while (c == '#' || isalnum(c)) {
	    digit = !!isdigit(c);
	    if (c == '#') {
		base = i;
		i = 0;
		if (!base)
		    base = 16;
	    } else {
		i *= base;
		if (digit)
		    i += (c - '0');
		else {
		    if (c >= 'a' && c <= 'z')
			c = (c - 'a') + 'A';
		    if (c - 'A' + 10 >= base) {
			error=OUT_BASE_ERROR;
			return 0;
		    }
		    i += (c - 'A' + 10);
		}
	    }
	    c=*++line;
	}
	obj->type=TYPE_NUM;
	obj->num=i;
	i=1;
    }
    else if(c=='\"') {
	end=first_valid(++line, '\"');
	if (*end) {
	    obj->type=TYPE_TXT;
	    obj->txt=ptrmcpy(obj->txt, line, end-line);
	    if (!REAL_ERROR) {
		ptrunescape(obj->txt);
		i=1;
		line=end+1;
	    }
	}
    }
    else if ((c=='$' || c=='@') && (c=line[1]) && (isalpha(c) || c=='_')) {
	varnode *named_var;                       /* Found named variable */

	if (*(line++) == '@') {
	    i = 0;
	    obj->type = TYPE_NUM_VAR;
	}
	else {
	    i = 1;
	    obj->type = TYPE_TXT_VAR;
	}
	end = line + 1;
	while ((c=*end) && (isalpha(c) || c=='_' || isdigit(c)))
	    end++;
	c = *end; *end = '\0';
	if (!(named_var = *lookup_varnode(line, i))) {
	    named_var = add_varnode(line, i);
	    if (REAL_ERROR)
		return 0;
	    if (opt_info) {
		PRINTF("#new variable: %s\n", line - 1);
	    }
	}
	*end = c;
	line = end;
	obj->num = named_var->index;
	i = 1;
    }
    else if (!strncmp(line, "timer", 5)) {
	obj->type = TYPE_NUM;
	update_now();
	obj->num = diff_vtime(&now, &ref_time);
	line += 5;
	i = 1;
    }
    else if (!strncmp(line, "map", 3)) {
	char buf[MAX_MAPLEN + 1];
	map_sprintf(buf);
	obj->type = TYPE_TXT;
	obj->txt = ptrmcpy(obj->txt, buf, strlen(buf));
	if (!REAL_ERROR) {
	    line += 3;
	    i = 1;
	}
    }
    else if (!strncmp(line, "noattr", 6)) {
	obj->type = TYPE_TXT;
	obj->txt = ptrmcpy(obj->txt, tty_modestandoff, strlen(tty_modestandoff));
	obj->txt = ptrmcat(obj->txt, tty_modenorm, strlen(tty_modenorm));
	if (!REAL_ERROR) {
	    line += 6;
	    i = 1;
	}
    }
    else
	error=NO_VALUE_ERROR;

    return (int)i;
}

static void check_delete(object *obj)
{
    if (obj->type==TYPE_TXT && obj->txt) {
	ptrdel(&obj->txt);
	obj->txt = NULL;
    }
}

static int exe_op(operator *op)
{
    object o1 = { 0 }, o2 = { 0 }, *p=NULL;
    long *l, delta;
    ptr *src = NULL, *dst = NULL, *start = NULL;
    int srclen;
    char *ssrc, *tmp;
    int ret=0, i=0, j=0, danger=0;

    switch (*op) {
      case op_comma:
	if (pop_obj(&o2) && pop_obj(&o1));
	else if (REAL_ERROR) break;
	check_delete(&o1);
	p=&o2;
	ret=1;
	break;
      case op_eq:
	if (pop_obj(&o2) && pop_obj(&o1));
	else if (REAL_ERROR) break;

	if (o2.type==TYPE_NUM_VAR) {
	    o2.num = *VAR[o2.num].num;
	    o2.type = TYPE_NUM;
	}

	if (o1.type==TYPE_NUM_VAR && o2.type==TYPE_NUM) {
	    *VAR[o1.num].num = o2.num;
	    p=&o2;
	    ret=1;
	}
	else if (o1.type==TYPE_TXT_VAR &&
		 (o2.type==TYPE_TXT || o2.type==TYPE_TXT_VAR)) {

	    if (o2.type==TYPE_TXT_VAR) {
		o2.txt = ptrdup(*VAR[o2.num].str);
		if (REAL_ERROR) break;
		o2.type=TYPE_TXT;
	    }

	    *VAR[o1.num].str = ptrcpy(*VAR[o1.num].str, o2.txt);
	    if (REAL_ERROR) break;
	    p=&o2;
	    ret=1;
	}
	else
	    error=SYNTAX_ERROR;
	break;
      case op_or_or_eq:
      case op_xor_xor_eq:
      case op_and_and_eq:
      case op_or_eq:
      case op_xor_eq:
      case op_and_eq:
      case op_lshift_eq:
      case op_rshift_eq:
      case op_plus_eq:
      case op_minus_eq:
      case op_times_eq:
      case op_div_eq:
      case op_ampersand_eq:
	if (pop_obj(&o2) && pop_obj(&o1));
	else if (REAL_ERROR) break;

	if (o2.type==TYPE_NUM_VAR) {
	    o2.num = *VAR[o2.num].num;
	    o2.type = TYPE_NUM;
	}

	if (o1.type==TYPE_NUM_VAR && o2.type==TYPE_NUM) {
	    l=VAR[o1.num].num;

	    switch (*op) {
	      case op_or_or_eq:    if ( o2.num) *l = 1;   else *l = !!*l; break;
	      case op_xor_xor_eq:  if ( o2.num) *l = !*l; else *l = !!*l; break;
	      case op_and_and_eq:  if (!o2.num) *l = 0;   else *l = !!*l; break;
	      case op_or_eq:       *l  |= o2.num; break;
	      case op_xor_eq:      *l  ^= o2.num; break;
	      case op_and_eq:      *l  &= o2.num; break;
	      case op_lshift_eq:   *l <<= o2.num; break;
	      case op_rshift_eq:   *l >>= o2.num; break;
	      case op_plus_eq:     *l  += o2.num; break;
	      case op_minus_eq:    *l  -= o2.num; break;
	      case op_times_eq:    *l  *= o2.num; break;
	      case op_div_eq:      *l  /= o2.num; break;
	      case op_ampersand_eq:
                if ((*l %= o2.num) < 0)
                    *l += o2.num;
                break;
	      default:
	          break;
	    }
	    o2.num=*l;
	    p=&o2;
	    ret=1;
	}
	else if (*op==op_plus_eq && o1.type==TYPE_TXT_VAR &&
		 (o2.type==TYPE_TXT || o2.type==TYPE_TXT_VAR)) {

	    if (o2.type==TYPE_TXT)
		src=o2.txt;
	    else
		src=*VAR[o2.num].str;

	    *VAR[o1.num].str = ptrcat(*VAR[o1.num].str, src);
	    check_delete(&o2);

	    dst = ptrdup(*VAR[o1.num].str);
	    if (REAL_ERROR) break;

	    o1.type=TYPE_TXT;
	    o1.txt=dst;
	    p=&o1;
	    ret=1;
	}
	else if (*op==op_times_eq && o1.type==TYPE_TXT_VAR &&
		 (o2.type==TYPE_NUM || o2.type==TYPE_NUM_VAR)) {

	    if (o2.type==TYPE_NUM_VAR) {
		o2.num = *VAR[o2.num].num;
		o2.type = TYPE_NUM;
	    }

	    if (o2.num < 0)
		error = OUT_RANGE_ERROR;
	    else if (o2.num == 0)
		ptrzero(*VAR[o1.num].str);
	    else if (o2.num == 1)
		;
	    else if (*VAR[o1.num].str && (delta = ptrlen(*VAR[o1.num].str))) {
		long n;
		*VAR[o1.num].str = ptrsetlen(*VAR[o1.num].str, delta*o2.num);
		tmp = ptrdata(*VAR[o1.num].str);
		for (n = 1; !error && n<o2.num; n++)
		    memcpy(tmp+n*delta, tmp, delta);
	    }

	    check_delete(&o2);
	    dst = ptrdup(*VAR[o1.num].str);
	    if (REAL_ERROR) break;

	    o1.type=TYPE_TXT;
	    o1.txt=dst;
	    p=&o1;
	    ret=1;
	}
	else
	    error=SYNTAX_ERROR;
	break;
      case op_or_or:
      case op_xor_xor:
      case op_and_and:
      case op_or:
      case op_xor:
      case op_and:
      case op_less:
      case op_less_eq:
      case op_greater:
      case op_greater_eq:
      case op_eq_eq:
      case op_not_eq:
      case op_lshift:
      case op_rshift:
      case op_minus:
      case op_plus:
      case op_times:
      case op_division:
      case op_ampersand:
	if (pop_obj(&o2) && pop_obj(&o1));
	else if (REAL_ERROR) break;

	if (o1.type==TYPE_NUM_VAR) {
	    o1.num = *VAR[o1.num].num;
	    o1.type = TYPE_NUM;
	}
	if (o2.type==TYPE_NUM_VAR) {
	    o2.num = *VAR[o2.num].num;
	    o2.type = TYPE_NUM;
	}

	if (o1.type==TYPE_NUM && o2.type==TYPE_NUM) {
	    if (!o2.num &&
		(*op==op_division || *op==op_ampersand)) {
		error=DIV_BY_ZERO_ERROR;
		break;
	    }

	    switch (*op) {
	      case op_less:        o1.num = o1.num <  o2.num ? 1 : 0; break;
	      case op_less_eq:     o1.num = o1.num <= o2.num ? 1 : 0; break;
	      case op_greater:     o1.num = o1.num >  o2.num ? 1 : 0; break;
	      case op_greater_eq:  o1.num = o1.num >= o2.num ? 1 : 0; break;
	      case op_eq_eq:       o1.num = o1.num == o2.num ? 1 : 0; break;
	      case op_not_eq:      o1.num = o1.num != o2.num ? 1 : 0; break;
	      case op_or_or:       o1.num = o1.num || o2.num; break;
	      case op_xor_xor:     if (o2.num) o1.num = !o1.num; break;
	      case op_and_and:     o1.num = o1.num && o2.num; break;
	      case op_or:          o1.num |= o2.num; break;
	      case op_xor:         o1.num ^= o2.num; break;
	      case op_and:         o1.num &= o2.num; break;
	      case op_lshift:      o1.num <<= o2.num; break;
	      case op_rshift:      o1.num >>= o2.num; break;
	      case op_minus:       o1.num -= o2.num; break;
	      case op_plus:        o1.num += o2.num; break;
	      case op_times:       o1.num *= o2.num; break;
	      case op_division:    o1.num /= o2.num; break;
	      case op_ampersand:
                if ((o1.num %= o2.num) < 0)
                    o1.num += o2.num;
                break;
	      default:
	          break;
	    }

	    p=&o1;
	    ret=1;
	}
	else if ((o1.type==TYPE_TXT || o1.type==TYPE_TXT_VAR) &&
		 (o2.type==TYPE_TXT || o2.type==TYPE_TXT_VAR)) {

	    if (o1.type==TYPE_TXT_VAR) {
		o1.txt = ptrdup(*VAR[o1.num].str);
        o1.type = TYPE_TXT; /* not a var anymore */
		if (REAL_ERROR) break;
	    }
	    dst = o1.txt;
	    if (o2.type==TYPE_TXT)
		src=o2.txt;
	    else
		src=*VAR[o2.num].str;

	    if (*op == op_plus) {
		dst = ptrcat(dst, src);
		o1.type = TYPE_TXT;
	    } else {
		o1.num = ptrcmp(dst, src);
		switch (*op) {
		  case op_minus:			      break;
		  case op_less:       o1.num = o1.num <  0; break;
		  case op_less_eq:    o1.num = o1.num <= 0; break;
		  case op_greater:    o1.num = o1.num >  0; break;
		  case op_greater_eq: o1.num = o1.num >= 0; break;
		  case op_eq_eq:      o1.num = o1.num == 0; break;
		  case op_not_eq:     o1.num = o1.num != 0; break;
		  default:
		    error=SYNTAX_ERROR;
		    p=NULL; ret=0; break;
		}
		check_delete(&o1);
        /* moved here because it interfered with allowing the dst ptr from
         * being freed, casing a very tiny memory leak */
		o1.type = TYPE_NUM;
	    }
	    check_delete(&o2);
	    if (!REAL_ERROR) {
		o1.txt = dst;
		p=&o1;
		ret=1;
	    }
	}
	else if (*op==op_times
		 && (o1.type==TYPE_TXT_VAR || o1.type==TYPE_TXT)
		 && o2.type==TYPE_NUM) {

	    if (o2.num > 0 && o1.type==TYPE_TXT_VAR) {
		o1.txt = ptrdup(*VAR[o1.num].str);
		if (REAL_ERROR) break;
	    }
	    dst = o1.txt;

	    if (o2.num < 0)
		error = OUT_RANGE_ERROR;
	    else if (o2.num == 0)
		ptrzero(dst);
	    else if (o2.num == 1)
		;
	    else if (dst && (delta = ptrlen(dst))) {
		long n;
		dst = ptrsetlen(dst, delta*o2.num);
		tmp = ptrdata(dst);
		for (n = 1; !error && n<o2.num; n++)
		    memcpy(tmp+n*delta, tmp, delta);
	    }
	    check_delete(&o2);
	    if (REAL_ERROR) break;

	    o1.type=TYPE_TXT;
	    o1.txt=dst;
	    p=&o1;
	    ret=1;
	}
	else
	    error=SYNTAX_ERROR;
	break;
      case op_colon_less:
      case op_colon_greater:
      case op_less_colon:
      case op_greater_colon:
      case op_colon:
      case op_point_less:
      case op_point_greater:
      case op_less_point:
      case op_greater_point:
      case op_point:
	if (pop_obj(&o2) && pop_obj(&o1));
	else if (REAL_ERROR) break;

	if (o2.type==TYPE_NUM_VAR) {
	    o2.num = *VAR[o2.num].num;
	    o2.type = TYPE_NUM;
	}

	if ((o1.type!=TYPE_TXT_VAR && o1.type!=TYPE_TXT) || o2.type!=TYPE_NUM) {
	    error=SYNTAX_ERROR;
	    break;
	}

	if (o2.num<=0) {
	    error=OUT_RANGE_ERROR;
	    break;
	}

	if (o1.type==TYPE_TXT_VAR) {
	    o1.type=TYPE_TXT;
	    o1.txt=dst=NULL;
	    src=start=*VAR[o1.num].str;
	}
	else {
	    /* Potentially dangerous: src and dst are overlapping */
	    src=dst=start=o1.txt;
	    danger=1;
	}

	if (!src) {
	    /* src == empty string. just return it */
	    check_delete(&o2);
	    o1.txt = src;
	    if (!REAL_ERROR) {
		p=&o1; ret=1;
            }
	    break;
	}

	srclen = ptrlen(src);
	ssrc = ptrdata(src);
	/* the point operators count characters, not bytes */
	int nchars = utf8_count(ssrc, srclen);
#define BYTES(n) ((int)utf8_offset(ssrc, srclen, (n)))

	switch (*op) {
	  case op_colon_less:
	    while (o2.num && srclen) {
		/* skip span of multiple word delimeters */
		while (srclen && IS_DELIM(*ssrc))
		    srclen--, ssrc++, j++;
		/* skip whole words */
		if (srclen && (tmp = memchrs(ssrc, srclen, DELIM, DELIM_LEN)))
		    i=tmp-ssrc, o2.num--, ssrc+=i, j+=i, srclen-=i;
		else break;
	    }

	    if (o2.num) { /* end of valid string before the n-th word */
		if (danger)
		    ;
		else
		    dst = ptrcpy(dst, start);
	    } else {
		if (danger)
		    ptrtrunc(dst, j);
		else
		    dst = ptrmcpy(dst, ptrdata(start), j);
	    }
	    break;
	  case op_colon:
	  case op_colon_greater:
	    o2.num--;
	    /* skip span of multiple word delimeters */
	    while (srclen && IS_DELIM(*ssrc))
		srclen--, ssrc++;
	    while (o2.num && srclen) {
		/* skip whole words */
		if (srclen && (tmp = memchrs(ssrc, srclen, DELIM, DELIM_LEN))) {
		    i=tmp-ssrc, o2.num--, ssrc+=i, srclen-=i;
		    /* skip span of multiple word delimeters */
		    while (srclen && IS_DELIM(*ssrc))
			srclen--, ssrc++;
		} else break;
	    }

	    if (o2.num)  /* end of valid string before the n-th word */
		ptrzero(dst);
	    else {
		if (*op==op_colon &&
		    (tmp = memchrs(ssrc, srclen, DELIM, DELIM_LEN))) {
		    dst = ptrmcpy(dst, ssrc, tmp-ssrc);
		}
		else
		    dst = ptrmcpy(dst, ssrc, srclen);
	    }
	    break;
	  case op_less_colon:
	    o2.num--;
	    while (o2.num && srclen) {
		/* skip span of multiple word delimeters */
		while (srclen && IS_DELIM(ssrc[srclen - 1]))
		    srclen--;
		/* skip whole words */
		if (srclen && (tmp=memrchrs(ssrc, srclen, DELIM, DELIM_LEN)))
		    o2.num--, srclen=tmp-ssrc;
		else break;
	    }

	    if (o2.num) /* end of valid string before the n-th word */
		ptrzero(dst);
	    else
		dst = ptrmcpy(dst, ssrc, srclen);
	    break;
	  case op_greater_colon:
	    while (o2.num && srclen) {
		/* skip span of multiple word delimeters */
		while (srclen && IS_DELIM(ssrc[srclen - 1]))
		    srclen--;
		/* skip whole words */
		if (srclen && (tmp=memrchrs(ssrc, srclen, DELIM, DELIM_LEN)))
		    o2.num--, srclen=tmp-ssrc;
		else break;
	    }

	    if (o2.num) /* end of valid string before the n-th word */
		dst = ptrcpy(dst, start);
	    else
		dst = ptrmcpy(dst, ssrc+srclen+1,
			      ptrlen(start) - (ssrc+srclen+1 - ptrdata(start)));
	    break;
	  case op_point:
	    j = BYTES(o2.num <= nchars ? o2.num-1 : nchars);
	    dst = ptrmcpy(dst, ssrc+j, BYTES(o2.num <= nchars ? o2.num : nchars) - j);
	    break;
	  case op_point_less:
	    j = BYTES(o2.num < nchars ? o2.num : nchars);
	    if (danger)
		ptrtrunc(dst, j);
	    else
		dst = ptrmcpy(dst, ssrc, j);
	    break;
	  case op_less_point:
	    j = nchars-o2.num+1;
	    j = j < 0 ? 0 : BYTES(j);
	    if (danger)
		ptrtrunc(dst, j);
	    else
		dst = ptrmcpy(dst, ssrc, j);
	    break;
	  case op_point_greater:
	    j = BYTES(o2.num-1 < nchars ? o2.num-1 : nchars);
	    dst = ptrmcpy(dst, ssrc+j, srclen-j);
	    break;
	  case op_greater_point:
	    j = nchars-o2.num;
	    j = j < 0 ? 0 : BYTES(j);
	    dst = ptrmcpy(dst, ssrc+j, srclen-j);
	    break;
	  default:
	      break;
	}
#undef BYTES
	check_delete(&o2);
	o1.txt = dst;
	if (!REAL_ERROR) {
	    p=&o1; ret=1;
        }
	break;
      case op_colon_question:
      case op_point_question:
	if (pop_obj(&o1));
	else if (REAL_ERROR) break;

	if (o1.type==TYPE_TXT)
	    src=o1.txt;
	else if (o1.type==TYPE_TXT_VAR)
	    src=*VAR[o1.num].str;
	else {
	    error=SYNTAX_ERROR;
	    break;
	}
	if (!src) {
	    /* empty string. return 0 */
	    check_delete(&o1);
	    o1.type=TYPE_NUM;
	    o1.num =0;
	    p=&o1;
	    ret=1;
	    break;
	}

	ssrc = ptrdata(src);
	srclen = ptrlen(src);

	if (*op==op_colon_question) {
	    o1.num = 0;
	    /* skip span of multiple word delimeters */
	    while (srclen && IS_DELIM(*ssrc))
		ssrc++, srclen--;
	    while (srclen) {
		/* skip whole words */
		if (srclen && (tmp=memchrs(ssrc, srclen, DELIM, DELIM_LEN))) {
		    i=tmp-ssrc, o1.num++, ssrc+=i, srclen-=i;
		    /* skip span of multiple word delimeters */
		    while (srclen && IS_DELIM(*ssrc))
			srclen--, ssrc++;
		} else {
		    srclen=0;
		    o1.num++;
		}
	    }
	}
	else
	    o1.num=utf8_count(ssrc, srclen);

	check_delete(&o1);
	o1.type=TYPE_NUM;
	p=&o1;
	ret=1;
	break;
      case op_question:
	if (pop_obj(&o2) && pop_obj(&o1));
	else if (REAL_ERROR) break;

	if (o1.type==TYPE_TXT)
	    src = o1.txt;
	else if (o1.type==TYPE_TXT_VAR)
	    src = *VAR[o1.num].str;
	else
	    error = SYNTAX_ERROR;

	if (o2.type==TYPE_TXT)
	    dst = o2.txt;
	else if (o2.type==TYPE_TXT_VAR)
	    dst = *VAR[o2.num].str;
	else
	    error = SYNTAX_ERROR;

	if (!error) {
	    if ((ssrc = ptrfind(src, dst)))
		i = (int)utf8_count(ptrdata(src), ssrc - ptrdata(src)) + 1;
	    else
		i = 0;
	    o1.type = TYPE_NUM;
	    o1.num = i;
	    p=&o1; ret=1;
	}
	check_delete(&o1);
	check_delete(&o2);
	break;
      case op_null:
      case op_another_null:
	if (pop_obj(&o2) && pop_obj(&o1));
	else if (REAL_ERROR) break;

	check_delete(&o1);
	check_delete(&o2);

	o1.type=0, o1.num=0, o1.txt=NULL;

	p=&o1;
	ret=1;
	break;
      case op_left_paren:
	error=MISSING_PAREN_ERROR;
	break;
      case op_right_paren:
	if (pop_op(op));
	else if (REAL_ERROR) break;

	if (*op!=op_left_paren)
	    error=MISMATCH_PAREN_ERROR;
	else
	    ret=1;

	break;
      case op_random:
      case op_pre_plus:
      case op_pre_minus:
      case op_not:
      case op_tilde:

	if (pop_obj(&o1));
	else if (REAL_ERROR) break;

	if (o1.type==TYPE_NUM_VAR) {
	    o1.num = *VAR[o1.num].num;
	    o1.type = TYPE_NUM;
	}
	if (o1.type==TYPE_NUM) {
	    if (*op==op_pre_minus)
		o1.num=-o1.num;
	    else if (*op==op_not)
		o1.num=!o1.num;
	    else if (*op==op_tilde)
		o1.num=~o1.num;
	    else if (*op==op_random) {
		if (o1.num <= 0) {
		    error=OUT_RANGE_ERROR;
		    break;
		} else
		    o1.num = random_below(o1.num);
	    }
	    p=&o1;
	    ret=1;
	}
	else
	    error=SYNTAX_ERROR;
	break;
      case op_attr:
	if (pop_obj(&o1));
	else if (REAL_ERROR) break;

	if (o1.type==TYPE_TXT_VAR) {
	    o1.txt = ptrdup(*VAR[o1.num].str);
	    if (REAL_ERROR) break;
	    o1.type = TYPE_TXT;
	}

	if (o1.type==TYPE_TXT) {
	    char dummy[ATTR_SGR_MAX]; /* attr_string must write somewhere */
	    struct attr attr = { 0 };

	    if (o1.txt != NULL && !attr_parse(ptrdata(o1.txt), &attr))
		error=BAD_ATTR_ERROR;
	    else {
		o1.txt = ptrsetlen(o1.txt, ATTR_SGR_MAX);
		if (REAL_ERROR) break;
		attr_string(&attr, ptrdata(o1.txt), dummy);
		ptrtrunc(o1.txt, strlen(ptrdata(o1.txt)));
		p=&o1;
		ret = 1;
	    }
	} else
	    error=NO_STRING_ERROR;
	break;

      case op_star:
      case op_print:
	if (pop_obj(&o1));
	else if (REAL_ERROR) break;

	if (o1.type==TYPE_NUM_VAR)
	    o1.num = *VAR[o1.num].num;
	else if (o1.type==TYPE_TXT_VAR)
	    o1.txt = *VAR[o1.num].str;

	if (o1.type==TYPE_NUM || o1.type==TYPE_NUM_VAR) {
	    o1.txt = NULL;
	    if (*op==op_print) {
		char buf[LONGLEN];
		sprintf(buf, "%ld", o1.num);
		o1.txt = ptrmcpy(o1.txt, buf, strlen(buf));
	    } else {
		/* character with that code point */
		char buf[UTF8_MAX];
		uint32_t cp = o1.num < 0 || o1.num > 0x10ffff
		    ? UNICODE_REPLACEMENT : (uint32_t)o1.num;
		o1.txt = ptrmcpy(o1.txt, buf, utf8_put(cp, buf));
	    }
	    if (REAL_ERROR) break;
	    o1.type = TYPE_TXT;
	    p=&o1; ret=1;
	}
	else if (o1.type==TYPE_TXT || o1.type==TYPE_TXT_VAR) {
	    if (*op==op_print) {
		if (o1.txt && ptrlen(o1.txt))
		    o1.num = atol(ptrdata(o1.txt));
		else
		    o1.num = 0;
	    } else {
		if (o1.txt && ptrlen(o1.txt)) {
		    /* code point of the first character */
		    uint32_t cp;
		    utf8_get(ptrdata(o1.txt), ptrlen(o1.txt), &cp);
		    o1.num = cp;
		} else
		    o1.num = 0;
	    }
	    check_delete(&o1);
	    o1.type = TYPE_NUM;
	    p=&o1; ret=1;
	}
	else
	    error=SYNTAX_ERROR;
	break;
      case op_pre_plus_plus:
      case op_post_plus_plus:
      case op_pre_minus_minus:
      case op_post_minus_minus:
	if (pop_obj(&o1));
	else if (REAL_ERROR) break;

	if (o1.type==TYPE_NUM_VAR) {
	    l=VAR[o1.num].num;
	    o1.type=TYPE_NUM;

	    if (*op==op_pre_plus_plus)
		o1.num=++*l;
	    else if (*op==op_post_plus_plus)
		o1.num=(*l)++;
	    else if (*op==op_pre_minus_minus)
		o1.num=--*l;
	    else
		o1.num=(*l)--;

	    p=&o1;
	    ret=1;
	}
	else
	    error=SYNTAX_ERROR;
	break;
      case op_a_circle:
      case op_dollar:
	if (pop_obj(&o1));
	else if (REAL_ERROR) break;

	if (*op == op_dollar)
	    delta = 1;
	else
	    delta = 0;

	if (o1.type==TYPE_NUM_VAR) {
	    o1.type=TYPE_NUM;
	    o1.num=*VAR[o1.num].num;
	}

	if (o1.type==TYPE_NUM) {
	    if (o1.num<-NUMVAR || o1.num>=NUMPARAM) {
		error=OUT_RANGE_ERROR;
		break;
	    }
	    o1.type= delta ? TYPE_TXT_VAR : TYPE_NUM_VAR;
	    p=&o1;
	    ret=1;
	} else {
	    varnode *named_var;
	    char c;

	    if (o1.type==TYPE_TXT_VAR)
		o1.txt = *VAR[o1.num].str;
	    else if (o1.type!=TYPE_TXT) {
		error=SYNTAX_ERROR;
		break;
	    }

	    if (o1.txt && (tmp=ptrdata(o1.txt)) &&
		((c=*tmp) == '_' || isalpha(c))) {
		tmp++;
		while ((c=*tmp) == '_' || isalnum(c))
		    tmp++;
	    }
	    if (!o1.txt || *tmp) {
		error=INVALID_NAME_ERROR;
		break;
	    }

	    if (!(named_var = *lookup_varnode(ptrdata(o1.txt), delta))) {
		named_var = add_varnode(ptrdata(o1.txt), delta);
		if (REAL_ERROR)
		    break;
		if (opt_info) {
		    PRINTF("#new variable: %c%s\n", delta
			   ? '$' : '@', ptrdata(o1.txt));
		}
	    }
	    o1.type= delta ? TYPE_TXT_VAR : TYPE_NUM_VAR;
	    p=&o1;
	    ret=1;
	}
	break;
      case op_pre_null:
      case op_post_null:
	ret=1;
	break;
      default:
	break;
    }

    if (REAL_ERROR) {
	check_delete(&o2);
	check_delete(&o1);
    }

    if (!REAL_ERROR) {
	if (!ret)
	    error=NOT_DONE_ERROR;
	else if (p) {
	    if (push_obj(p))
		;
	    else
		check_delete(p);
	}
    }

    if (REAL_ERROR)
	return 0;

    return ret;
}

static int whichfirst(operator *op1, operator *op2)
{
    int p1, p2;

    p1=op_list[*op1].priority;
    p2=op_list[*op2].priority;
    if (p1!=p2)
	return p1>p2 ? -1 : 1;

    p1 = op_list[*op1].assoc == LEFT;
    return p1 ? -1 : 1;
}

static int compare_and_unload(operator *op)
{
    int first=0;
    operator new;

    if (REAL_ERROR || stk.curr_op<0)
	return 1;

    while (stk.curr_op>=0 && pop_op(&new) && !REAL_ERROR &&
	   (first = whichfirst(&new, op)) == -1 &&
	   (first = 0, exe_op(&new))
	   );

    if (!REAL_ERROR) {
	if (!first)
	    return 1;
	else
	    return push_op(&new);
    } else
	return 0;
}

static int _eval(int mindepth)
{
    operator op;
    char endreached = 0;

    for (;;) {
        object obj = { 0 };

	while (*line==' ') line++;
	if (!*line || *line == CMDSEP)
	    endreached = 1;

	while (check_operator(LEFT, &op, mindepth) && push_op(&op) &&
	      !endreached) {

	    if (error) return 0;
	    while (*line==' ') line++;
	    if (!*line || *line == CMDSEP)
		endreached = 1;
	}

	if (!endreached && check_object(&obj) && push_obj(&obj));
	else if (error) return 0;

	while (*line==' ') line++;
	if (!*line || *line == CMDSEP)
	    endreached = 1;

	while (check_operator(RIGHT, &op, mindepth) && compare_and_unload(&op) &&
	      exe_op(&op) && depth>=mindepth && !endreached) {

	    if (error) return 0;
	    while (*line==' ')
		line++;
	    if (!*line || *line == CMDSEP)
		endreached = 1;
	}
	if (error) return 0;

	if (endreached || depth < mindepth)
	    break;

	if (check_operator(BINARY, &op, mindepth) &&
	    compare_and_unload(&op) && push_op(&op));
	else if (error) return 0;
    }
    return 1;
}

int eval_any(long *lres, ptr **pres, char **what)
{
    int printmode;
    long val;
    ptr *txt;
    object res = { 0 };

    if (pres)
	printmode = PRINT_AS_PTR;
    else if (lres)
	printmode = PRINT_AS_LONG;
    else
	printmode = PRINT_NOTHING;

    error=0;
    stk.curr_obj=stk.curr_op=-1;
    line = *what;

    depth = 0;
    (void)_eval(0);

    /* an empty expression, such as in "#(" */
    if (!error && stk.curr_obj < 0)
        error = NO_VALUE_ERROR;
    if (!error)
	(void)pop_obj(&res);
    if (error) {
	if (opt_debug) {
	    PRINTF("#result not available\n");
	}
    } else if (printmode!=PRINT_NOTHING || opt_debug) {
	if (res.type==TYPE_NUM || res.type==TYPE_NUM_VAR) {

	    val = res.type==TYPE_NUM ? res.num : *VAR[res.num].num;

	    if (printmode==PRINT_AS_PTR) {
		*pres = ptrsetlen(*pres, LONGLEN);
		if (!MEM_ERROR) {
		    sprintf(ptrdata(*pres), "%ld", val);
		    (*pres)->len = strlen(ptrdata(*pres));
		}
	    } else if (printmode==PRINT_AS_LONG)
		*lres=val;

	    if (opt_debug) {
		if (error) {
		    PRINTF("#result not available\n");
		} else {
		    PRINTF("#result: %ld\n", val);
		}
	    }
	} else {
	    txt = res.type==TYPE_TXT ? res.txt : *VAR[res.num].str;
	    if (printmode==PRINT_AS_PTR) {
		if (txt && *ptrdata(txt)) {
		    if (res.type == TYPE_TXT)
			/* shortcut! */
			*pres = txt;
		    else
			*pres = ptrcpy(*pres, txt);
		} else
		    ptrzero(*pres);
	    }
	    if (opt_debug) {
		if (error) {
		    PRINTF("#result not available\n");
		} else if (txt && *ptrdata(txt)) {
		    PRINTF("#result: %s\n", ptrdata(txt));
		} else {
		    PRINTF("#result empty\n");
		}
	    }
	}
    }
    *what=line;

    if (!error) {
	if (printmode==PRINT_AS_PTR && res.type == TYPE_TXT
	    && res.txt && ptrdata(res.txt))
	    /* shortcut! */
	    ;
	else
	    check_delete(&res);
    } else {
	while (stk.curr_obj>=0) {
	    pop_obj(&res);
	    check_delete(&res);
	}
	res.type = 0;
    }

    if (res.type==TYPE_TXT_VAR)
	res.type = TYPE_TXT;
    else if (res.type==TYPE_NUM_VAR)
	res.type = TYPE_NUM;

    return res.type;
}

int evalp(ptr **res, char **what)
{
    return eval_any((long *)0, res, what);
}

int evall(long *res, char **what)
{
    return eval_any(res, (ptr **)0, what);
}

int evaln(char **what)
{
    return eval_any((long *)0, (ptr **)0, what);
}

