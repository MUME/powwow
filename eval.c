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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <unistd.h>
#include <time.h>
#include <sys/types.h>
#include <sys/time.h>
#include <limits.h>

#include "defines.h"
#include "main.h"
#include "utils.h"
#include "cmd2.h"
#include "list.h"
#include "map.h"
#include "tty.h"
#include "edit.h"
#include "eval.h"

#ifdef USE_RANDOM
#  define get_random random
#  define init_random srandom
#else
#  define get_random lrand48
#  define init_random srand48
#endif

typedef struct {
  int  type;
  long num;      /* used for numeric types or as index for all variables */
  ptr  txt;      /* used for text types */
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
  null=0, comma, eq, or_or_eq, xor_xor_eq, and_and_eq, or_eq, xor_eq, and_eq,
  lshift_eq, rshift_eq, plus_eq, minus_eq, times_eq, div_eq, ampersand_eq,
  or_or, xor_xor, and_and, or, xor, and,
  less, less_eq, greater, greater_eq, eq_eq, not_eq,
  lshift, rshift, plus, minus, times, division, ampersand,

  colon_less, colon_greater, less_colon, greater_colon,
  point_less, point_greater, less_point, greater_point,
  colon, point, question, another_null,

  left_paren, right_paren, not, tilde,
  pre_plus_plus, post_plus_plus, pre_minus_minus, post_minus_minus,
  star, print, _random_, _attr_, colon_question, point_question,
  pre_plus, pre_minus, a_circle, dollar, pre_null, post_null
};

typedef enum op_codes operator;

typedef struct {
    char priority, assoc, syntax, *name;
    operator code;
} operator_list;

typedef struct {
  object obj[MAX_STACK];
  int curr_obj;
  operator op[MAX_STACK];
  int curr_op;
} stack;

char *error_msg[] = {
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

operator_list op_list[] = {
    { 0, 0,     0,	    "",     null		},
	
    { 1, LEFT,  BINARY,     ",",    comma		},
	
    { 2, RIGHT, BINARY,     "=",    eq			},
    { 2, RIGHT, BINARY,     "||=",  or_or_eq		},
    { 2, RIGHT, BINARY,     "^^=",  xor_xor_eq		},
    { 2, RIGHT, BINARY,     "&&=",  and_and_eq		},
    { 2, RIGHT, BINARY,     "|=",   or_eq		},
    { 2, RIGHT, BINARY,     "^=",   xor_eq		},
    { 2, RIGHT, BINARY,     "&=",   and_eq		},
    { 2, RIGHT, BINARY,     "<<=",  lshift_eq		},
    { 2, RIGHT, BINARY,     ">>=",  rshift_eq		},
    { 2, RIGHT, BINARY,     "+=",   plus_eq		},
    { 2, RIGHT, BINARY,     "-=",   minus_eq		},
    { 2, RIGHT, BINARY,     "*=",   times_eq		},
    { 2, RIGHT, BINARY,     "/=",   div_eq		},
    { 2, RIGHT, BINARY,     "%=",   ampersand_eq	},
	
    { 3, LEFT,  BINARY,     "||",   or_or		},
	
    { 4, LEFT,  BINARY,     "^^",   xor_xor		},
	
    { 5, LEFT,  BINARY,     "&&",   and_and		},
	
    { 6, LEFT,  BINARY,     "|",    or			},
	
    { 7, LEFT,  BINARY,     "^",    xor			},
	
    { 8, LEFT,  BINARY,     "&",    and			},
	
    { 9, LEFT,  BINARY,     "<",    less		},
    { 9, LEFT,  BINARY,     "<=",   less_eq		},
    { 9, LEFT,  BINARY,     ">",    greater		},
    { 9, LEFT,  BINARY,     ">=",   greater_eq		},
    { 9, LEFT,  BINARY,     "==",   eq_eq		},
    { 9, LEFT,  BINARY,     "!=",   not_eq		},
	
    {10, LEFT,  BINARY,     "<<",   lshift		},
    {10, LEFT,  BINARY,     ">>",   rshift		},
	
    {11, LEFT,  BINARY,     "+",    plus		},
    {11, LEFT,  BINARY,     "-",    minus		},
	
    {12, LEFT,  BINARY,     "*",    times		},
    {12, LEFT,  BINARY,     "/",    division		},
    {12, LEFT,  BINARY,     "%",    ampersand		},
	
    {14, LEFT,  BINARY,     ":<",   colon_less		},
    {14, LEFT,  BINARY,     ":>",   colon_greater	},
    {14, LEFT,  BINARY,     "<:",   less_colon		},
    {14, LEFT,  BINARY,     ">:",   greater_colon	},
    {14, LEFT,  BINARY,     ".<",   point_less		},
    {14, LEFT,  BINARY,     ".>",   point_greater	},
    {14, LEFT,  BINARY,     "<.",   less_point		},
    {14, LEFT,  BINARY,     ">.",   greater_point	},
    {14, LEFT,  BINARY,     ":",    colon		},
    {14, LEFT,  BINARY,     ".",    point		},
    {14, LEFT,  BINARY,     "?",    question		},
	
    { 0, 0,     0,	    "",     another_null	},
	
    { 0, RIGHT, PRE_UNARY,  "(",    left_paren		},
    { 0, RIGHT, POST_UNARY, ")",    right_paren		},
	
    {13, RIGHT, PRE_UNARY,  "!",    not			},
    {13, RIGHT, PRE_UNARY,  "~",    tilde		},
    {13, RIGHT, PRE_UNARY,  "++",   pre_plus_plus	},
    {13, RIGHT, POST_UNARY, "++",   post_plus_plus	},
    {13, RIGHT, PRE_UNARY,  "--",   pre_minus_minus	},
    {13, RIGHT, POST_UNARY, "--",   post_minus_minus	},
    {13, RIGHT, PRE_UNARY,  "*",    star		},
    {13, RIGHT, PRE_UNARY,  "%",    print		},
    {13, RIGHT, PRE_UNARY,  "rand", _random_		},
    {13, RIGHT, PRE_UNARY,  "attr", _attr_		},
	
    {14, LEFT,  PRE_UNARY,  ":?",   colon_question	},
    {14, LEFT,  PRE_UNARY,  ".?",   point_question	},
	
    {15, RIGHT, PRE_UNARY,  "+",    pre_plus		},
    {15, RIGHT, PRE_UNARY,  "-",    pre_minus		},
    {15, RIGHT, PRE_UNARY,  "@",    a_circle		},
    {15, RIGHT, PRE_UNARY,  "$",    dollar		},
	
    { 0, 0,     PRE_UNARY,  "",     pre_null		},
    { 0, 0,     POST_UNARY, "",     post_null		}
};

static stack stk;
static char *line;
static int depth;
int error;

void print_error __P1 (int,err_num)
{
    clear_input_line(1);
    if (error == NO_MEM_ERROR) {
	tty_printf("#system call error: %s (%d", "malloc", ENOMEM);
	tty_printf(": %s)\n", strerror(ENOMEM));
    } else
	tty_printf("#error: %s.\n", error_msg[err_num]);
}

static int push_op __P1 (operator *,op)
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

static int pop_op __P1 (operator *,op)
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

static int push_obj __P1 (object *,obj)
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

static int pop_obj __P1 (object *,obj)
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

static int check_operator __P3 (char,side, operator *,op, int,mindepth)
{
    int i, max, len;
    operator match;
    char *name, c, d;
    
    if (!(c=*line) || c == CMDSEP) {
	*op = side==BINARY ? null : side==LEFT ? pre_null : post_null;
	return 1;
    }
    else if ((c=='$' || c=='@') && (d=line[1]) && (isalpha(d) || d=='_'))
	return 0;           /* Danger! found named variable */
    
    else if (side==LEFT && c=='(') {
	line++;
	depth++;
	*op=left_paren;
	return 1;
    }
    else if (side==RIGHT && c==')') {
	if (--depth >= mindepth) {
	    line++;
	    *op=right_paren;
	}
	else 		   /* exit without touching the parenthesis */
	    *op=post_null;
	return 1;
    }
    else if (side==RIGHT && (c=='}' || c==']') && depth == mindepth) {
	/* allow also exiting with a '}' or a ']' */
	--depth;
	*op=post_null;
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
	*op= side==BINARY ? null : side==PRE_UNARY ? pre_null : post_null;
	if (side==BINARY)
	    error=NO_OPERATOR_ERROR;
    }
    return 0;
}

static int check_object __P1 (object *,obj)
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
	    if (echo_int) {
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

static void check_delete __P1 (object *,obj)
{
    if (obj->type==TYPE_TXT && obj->txt) {
	ptrdel(obj->txt);
	obj->txt = NULL;
    }
}

static int exe_op __P1 (operator *,op)
{
    object o1, o2, *p=NULL;
    long *l, rnd, delta;
    ptr src = NULL, dst = NULL, start = NULL;
    int srclen;
    char *ssrc, *tmp;
    int ret=0, i=0, j=0, danger=0;
    
    o1.txt = o2.txt = NULL;
    
    switch ((int)*op) {
      case (int)comma:
	if (pop_obj(&o2) && pop_obj(&o1));
	else if (REAL_ERROR) break;
	check_delete(&o1);
	p=&o2;
	ret=1;
	break;
      case (int)eq:
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
      case (int)or_or_eq:
      case (int)xor_xor_eq:
      case (int)and_and_eq:
      case (int)or_eq:
      case (int)xor_eq:
      case (int)and_eq:
      case (int)lshift_eq:
      case (int)rshift_eq:
      case (int)plus_eq:
      case (int)minus_eq:
      case (int)times_eq:
      case (int)div_eq:
      case (int)ampersand_eq:
	if (pop_obj(&o2) && pop_obj(&o1));
	else if (REAL_ERROR) break;
	
	if (o2.type==TYPE_NUM_VAR) {
	    o2.num = *VAR[o2.num].num;
	    o2.type = TYPE_NUM;
	}
	
	if (o1.type==TYPE_NUM_VAR && o2.type==TYPE_NUM) {
	    l=VAR[o1.num].num;
	    
	    switch ((int)*op) {
	      case (int)or_or_eq:  if ( o2.num) *l = 1;   else *l = !!*l; break;
	      case (int)xor_xor_eq:if ( o2.num) *l = !*l; else *l = !!*l; break;
	      case (int)and_and_eq:if (!o2.num) *l = 0;   else *l = !!*l; break;
	      case (int)or_eq:     *l  |= o2.num; break;
	      case (int)xor_eq:    *l  ^= o2.num; break;
	      case (int)and_eq:    *l  &= o2.num; break;
	      case (int)lshift_eq: *l <<= o2.num; break;
	      case (int)rshift_eq: *l >>= o2.num; break;
	      case (int)plus_eq:   *l  += o2.num; break;
	      case (int)minus_eq:  *l  -= o2.num; break;
	      case (int)times_eq:  *l  *= o2.num; break;
	      case (int)div_eq:    *l  /= o2.num; break;
	      case (int)ampersand_eq:
		if ((*l %= o2.num) < 0) *l += o2.num; break;
	    }
	    o2.num=*l;
	    p=&o2;
	    ret=1;
	}
	else if (*op==plus_eq && o1.type==TYPE_TXT_VAR &&
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
	else if (*op==times_eq && o1.type==TYPE_TXT_VAR &&
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
      case (int)or_or:
      case (int)xor_xor:
      case (int)and_and:
      case (int)or:
      case (int)xor:
      case (int)and:
      case (int)less:
      case (int)less_eq:
      case (int)greater:
      case (int)greater_eq:
      case (int)eq_eq:
      case (int)not_eq:
      case (int)lshift:
      case (int)rshift:
      case (int)minus:
      case (int)plus:
      case (int)times:
      case (int)division:
      case (int)ampersand:
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
		(*op==division || *op==ampersand)) {
		error=DIV_BY_ZERO_ERROR;
		break;
	    }
	    
	    switch ((int)*op) {
	      case (int)less:      o1.num = o1.num <  o2.num ? 1 : 0; break;
	      case (int)less_eq:   o1.num = o1.num <= o2.num ? 1 : 0; break;
	      case (int)greater:   o1.num = o1.num >  o2.num ? 1 : 0; break;
	      case (int)greater_eq:o1.num = o1.num >= o2.num ? 1 : 0; break;
	      case (int)eq_eq:     o1.num = o1.num == o2.num ? 1 : 0; break;
	      case (int)not_eq:    o1.num = o1.num != o2.num ? 1 : 0; break;
	      case (int)or_or:     o1.num = o1.num || o2.num; break;
	      case (int)xor_xor:if (o2.num) o1.num = !o1.num; break;
	      case (int)and_and:   o1.num = o1.num && o2.num; break;
	      case (int)or:  o1.num |= o2.num; break;
	      case (int)xor: o1.num ^= o2.num; break;
	      case (int)and: o1.num &= o2.num; break;
	      case (int)lshift:    o1.num <<= o2.num; break;
	      case (int)rshift:    o1.num >>= o2.num; break;
	      case (int)minus:   o1.num -= o2.num; break;
	      case (int)plus:    o1.num += o2.num; break;
	      case (int)times:   o1.num *= o2.num; break;
	      case (int)division:o1.num /= o2.num; break;
	      case (int)ampersand:
		if ((o1.num %= o2.num) < 0) o1.num += o2.num; break;
	    }
	    
	    p=&o1;
	    ret=1;
	}
	else if ((o1.type==TYPE_TXT || o1.type==TYPE_TXT_VAR) &&
		 (o2.type==TYPE_TXT || o2.type==TYPE_TXT_VAR)) {
	    
	    if (o1.type==TYPE_TXT_VAR) {
		o1.txt = ptrdup(*VAR[o1.num].str);
		if (REAL_ERROR) break;
	    }
	    dst = o1.txt;
	    if (o2.type==TYPE_TXT)
		src=o2.txt;
	    else
		src=*VAR[o2.num].str;
   
	    if (*op == plus) {
		dst = ptrcat(dst, src);
		o1.type = TYPE_TXT;
	    } else {
		o1.type = TYPE_NUM;
		o1.num = ptrcmp(dst, src);
		switch ((int)*op) {
		  case (int)minus:			      break;
		  case (int)less:       o1.num = o1.num <  0; break;
		  case (int)less_eq:    o1.num = o1.num <= 0; break;
		  case (int)greater:    o1.num = o1.num >  0; break;
		  case (int)greater_eq: o1.num = o1.num >= 0; break;
		  case (int)eq_eq:      o1.num = o1.num == 0; break;
		  case (int)not_eq:     o1.num = o1.num != 0; break;
		  default:
		    error=SYNTAX_ERROR;
		    p=NULL; ret=0; break;
		}
		check_delete(&o1);
	    }
	    check_delete(&o2);
	    if (!REAL_ERROR) {
		o1.txt = dst;
		p=&o1;
		ret=1;
	    }
	}
	else if (*op==times
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
      case (int)colon_less:
      case (int)colon_greater:
      case (int)less_colon:
      case (int)greater_colon:
      case (int)colon:
      case (int)point_less:
      case (int)point_greater:
      case (int)less_point:
      case (int)greater_point:
      case (int)point:
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
	    if (!REAL_ERROR)
		p=&o1; ret=1;
	    break;
	}
	
	srclen = ptrlen(src);
	ssrc = ptrdata(src);
	
	switch ((int)*op) {
	  case (int)colon_less:
	    while (o2.num && srclen) {
		/* skip span of multiple word delimeters */
		while (srclen && memchr(DELIM, *ssrc, DELIM_LEN))
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
	  case (int)colon:
	  case (int)colon_greater:
	    o2.num--;
	    /* skip span of multiple word delimeters */
	    while (srclen && memchr(DELIM, *ssrc, DELIM_LEN))
		srclen--, ssrc++;
	    while (o2.num && srclen) {
		/* skip whole words */
		if (srclen && (tmp = memchrs(ssrc, srclen, DELIM, DELIM_LEN))) {
		    i=tmp-ssrc, o2.num--, ssrc+=i, srclen-=i;
		    /* skip span of multiple word delimeters */
		    while (srclen && memchr(DELIM, *ssrc, DELIM_LEN))
			srclen--, ssrc++;
		} else break;
	    }
	    
	    if (o2.num)  /* end of valid string before the n-th word */
		ptrzero(dst);
	    else {
		if (*op==colon &&
		    (tmp = memchrs(ssrc, srclen, DELIM, DELIM_LEN))) {
		    dst = ptrmcpy(dst, ssrc, tmp-ssrc);
		}
		else
		    dst = ptrmcpy(dst, ssrc, srclen);
	    }
	    break;
	  case (int)less_colon:
	    o2.num--;
	    while (o2.num && srclen) {
		/* skip span of multiple word delimeters */
		while (srclen && memchr(DELIM, ssrc[srclen], DELIM_LEN))
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
	  case (int)greater_colon:
	    while (o2.num && srclen) {
		/* skip span of multiple word delimeters */
		while (srclen && memchr(DELIM, ssrc[srclen], DELIM_LEN))
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
	  case (int)point:
	    dst = ptrmcpy(dst, ssrc+o2.num-1, 1);
	    break;
	  case (int)point_less:
	    j = o2.num < srclen ? o2.num : srclen;
	    if (danger)
		ptrtrunc(dst, j);
	    else
		dst = ptrmcpy(dst, ssrc, j);
	    break;
	  case (int)less_point:
	    j = srclen-o2.num+1;
	    if (j < 0)
		j = 0;
	    if (danger)
		ptrtrunc(dst, j);
	    else
		dst = ptrmcpy(dst, ssrc, j);
	    break;
	  case (int)point_greater:
	    j = o2.num-1 < srclen ? o2.num-1 : srclen;
	    dst = ptrmcpy(dst, ssrc+j, srclen-j);
	    break;
	  case (int)greater_point:
	    j = srclen-o2.num;
	    if (j < 0)
		j = 0;
	    dst = ptrmcpy(dst, ssrc+j, srclen-j);
	    break;
	}
	check_delete(&o2);
	o1.txt = dst;
	if (!REAL_ERROR)
	    p=&o1; ret=1;
	break;
      case (int)colon_question:
      case (int)point_question:
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
	
	if (*op==colon_question) {
	    o1.num = 0;
	    /* skip span of multiple word delimeters */
	    while (srclen && memchr(DELIM, *ssrc, DELIM_LEN))
		ssrc++, srclen--;
	    while (srclen) {
		/* skip whole words */
		if (srclen && (tmp=memchrs(ssrc, srclen, DELIM, DELIM_LEN))) {
		    i=tmp-ssrc, o1.num++, ssrc+=i, srclen-=i;
		    /* skip span of multiple word delimeters */
		    while (srclen && memchr(DELIM, *ssrc, DELIM_LEN))
			srclen--, ssrc++;
		} else {
		    srclen=0;
		    o1.num++;
		}
	    }
	}
	else
	    o1.num=srclen;
	
	check_delete(&o1);
	o1.type=TYPE_NUM;
	p=&o1;
	ret=1;
	break;
      case (int)question:
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
		i = (int)(ssrc - ptrdata(src)) + 1;
	    else
		i = 0;
	    o1.type = TYPE_NUM;
	    o1.num = i;
	    p=&o1; ret=1;
	}
	check_delete(&o1);
	check_delete(&o2);
	break;
      case (int)null:
      case (int)another_null:
	if (pop_obj(&o2) && pop_obj(&o1));
	else if (REAL_ERROR) break;
	
	check_delete(&o1);
	check_delete(&o2);
	
	o1.type=0, o1.num=0, o1.txt=NULL;
	
	p=&o1;
	ret=1;
	break;
      case (int)left_paren:
	error=MISSING_PAREN_ERROR;
	break;
      case (int)right_paren:
	if (pop_op(op));
	else if (REAL_ERROR) break;
	
	if (*op!=left_paren)
	    error=MISMATCH_PAREN_ERROR;
	else
	    ret=1;
	
	break;
      case (int)_random_:
#ifdef NO_RANDOM
	error = NOT_SUPPORTED_ERROR;
	break;
#endif
      case (int)pre_plus:
      case (int)pre_minus:
      case (int)not:
      case (int)tilde:
	
	if (pop_obj(&o1));
	else if (REAL_ERROR) break;
	
	if (o1.type==TYPE_NUM_VAR) {
	    o1.num = *VAR[o1.num].num;
	    o1.type = TYPE_NUM;
	}
	if (o1.type==TYPE_NUM) {
	    if (*op==pre_minus)
		o1.num=-o1.num;
	    else if (*op==not)
		o1.num=!o1.num;
	    else if (*op==tilde)
		o1.num=~o1.num;
#ifndef NO_RANDOM
	    else if (*op==_random_) {
		if (o1.num <= 0) {
		    error=OUT_RANGE_ERROR;
		    break;
		} else {
		    delta = LONG_MAX - LONG_MAX % o1.num;
		    while (rnd = get_random(), rnd > delta);
		    /* skip numbers that would alterate distribution */
		    o1.num = rnd / (delta / o1.num);
		}
	    }
#endif
	    p=&o1;
	    ret=1;
	}
	else
	    error=SYNTAX_ERROR;
	break;
      case (int)_attr_:
	if (pop_obj(&o1));
	else if (REAL_ERROR) break;
	
	if (o1.type==TYPE_TXT_VAR) {
	    o1.txt = ptrdup(*VAR[o1.num].str);
	    if (REAL_ERROR) break;
	    o1.type = TYPE_TXT;
	}     
	
	if (o1.type==TYPE_TXT) {
	    char dummy[CAPLEN]; /* just because attr_string must write somewhere */
	    
	    if (o1.txt)
		i = parse_attributes(ptrdata(o1.txt));
	    else
		i = NOATTRCODE;
	    if (i == -1)
		error=BAD_ATTR_ERROR;
	    else {
		o1.txt = ptrsetlen(o1.txt, CAPLEN);
		if (REAL_ERROR) break;
		attr_string(i, ptrdata(o1.txt), dummy);
		ptrtrunc(o1.txt, strlen(ptrdata(o1.txt)));
		p=&o1;
		ret = 1;
	    }
	} else
	    error=NO_STRING_ERROR;
	break;
	
      case (int)star:
      case (int)print:
	if (pop_obj(&o1));
	else if (REAL_ERROR) break;
	
	if (o1.type==TYPE_NUM_VAR)
	    o1.num = *VAR[o1.num].num;
	else if (o1.type==TYPE_TXT_VAR)
	    o1.txt = *VAR[o1.num].str;
	
	if (o1.type==TYPE_NUM || o1.type==TYPE_NUM_VAR) {
	    o1.txt = NULL;
	    if (*op==print) {
		char buf[LONGLEN];
		sprintf(buf, "%ld", o1.num);
		o1.txt = ptrmcpy(o1.txt, buf, strlen(buf));
	    } else {
		char buf = (char)o1.num;
		o1.txt = ptrmcpy(o1.txt, &buf, 1);
	    }
	    if (REAL_ERROR) break;
	    o1.type = TYPE_TXT;
	    p=&o1; ret=1;
	}
	else if (o1.type==TYPE_TXT || o1.type==TYPE_TXT_VAR) {
	    if (*op==print) {
		if (o1.txt && ptrlen(o1.txt))
		    o1.num = atol(ptrdata(o1.txt));
		else
		    o1.num = 0;
	    } else {
		if (o1.txt && ptrlen(o1.txt))
		    o1.num = (long)(byte)*ptrdata(o1.txt);
		else
		    o1.num = 0;
	    }
	    check_delete(&o1);
	    o1.type = TYPE_NUM;
	    p=&o1; ret=1;
	}
	else 
	    error=SYNTAX_ERROR;
	break;
      case (int)pre_plus_plus:
      case (int)post_plus_plus:
      case (int)pre_minus_minus:
      case (int)post_minus_minus:
	if (pop_obj(&o1));
	else if (REAL_ERROR) break;
	
	if (o1.type==TYPE_NUM_VAR) {
	    l=VAR[o1.num].num;
	    o1.type=TYPE_NUM;
	    
	    if (*op==pre_plus_plus)
		o1.num=++*l;
	    else if (*op==post_plus_plus)
		o1.num=(*l)++;
	    else if (*op==pre_minus_minus)
		o1.num=--*l;
	    else
		o1.num=(*l)--;
	    
	    p=&o1;
	    ret=1;
	}
	else
	    error=SYNTAX_ERROR;
	break;
      case (int)a_circle:
      case (int)dollar:
	if (pop_obj(&o1));
	else if (REAL_ERROR) break;
	
	if (*op == dollar)
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
		if (echo_int) {
		    PRINTF("#new variable: %c%s\n", delta
			   ? '$' : '@', ptrdata(o1.txt));
		}
	    }
	    o1.type= delta ? TYPE_TXT_VAR : TYPE_NUM_VAR;	    
	    p=&o1;
	    ret=1;
	}
	break;
      case (int)pre_null:
      case (int)post_null:
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

static int whichfirst __P2 (operator *,op1, operator *,op2)
{
    int p1, p2;
    
    p1=op_list[*op1].priority;
    p2=op_list[*op2].priority;
    if (p1!=p2)
	return p1>p2 ? -1 : 1;
    
    p1 = op_list[*op1].assoc == LEFT;
    return p1 ? -1 : 1;
}

static int compare_and_unload __P1 (operator *,op)
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

static int _eval __P1 (int,mindepth)
{
    operator op;
    object obj;
    char endreached = 0;
    
    for (;;) {
	memzero(&obj, sizeof(obj));
	
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

int eval_any __P3 (long *,lres, ptr *,pres, char **,what)
{
    int printmode;
    long val;
    ptr txt;
    object res;
    
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

int evalp __P2 (ptr *,res, char **,what)
{
    return eval_any((long *)0, res, what);
}

int evall __P2 (long *,res, char **,what)
{
    return eval_any(res, (ptr *)0, what);
}

int evaln __P1 (char **,what)
{
    return eval_any((long *)0, (ptr *)0, what);
}

