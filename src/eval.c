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

/*
 * An operand that is a variable points to it with num_ref or txt_ref, which
 * is safe because an expression may create variables but never delete one
 * or run a command, and an evaluation returns values, not references to
 * variables:
 * - variables are deleted only by commands (#var @x=, #reset var), and the
 *   frames of @0..@9 and $0..$9 change only around the commands run by
 *   aliases, actions, #for and #while; so a pointer from one call of
 *   eval_any() does not outlive the variable;
 * - creating a variable does not move the others, as each varnode is its
 *   own allocation;
 * - txt_ref is the address of the field that holds the text, which stays
 *   put when assigning reallocates the text.
 */
typedef struct {
    int type;
    long num;                   /* the value of TYPE_NUM */
    ptr *txt;                   /* the value of TYPE_TXT */
    long *num_ref;              /* the variable of TYPE_NUM_VAR */
    ptr **txt_ref;              /* the variable of TYPE_TXT_VAR */
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
    op_null = 0, op_comma, op_eq,
    op_or_or_eq, op_xor_xor_eq, op_and_and_eq, op_or_eq, op_xor_eq, op_and_eq,
    op_lshift_eq, op_rshift_eq, op_plus_eq, op_minus_eq, op_times_eq,
    op_div_eq, op_ampersand_eq,
    op_or_or, op_xor_xor, op_and_and, op_or, op_xor, op_and,
    op_less, op_less_eq, op_greater, op_greater_eq, op_eq_eq, op_not_eq,
    op_lshift, op_rshift, op_plus, op_minus, op_times, op_division,
    op_ampersand,

    op_colon_less, op_colon_greater, op_less_colon, op_greater_colon,
    op_point_less, op_point_greater, op_less_point, op_greater_point,
    op_colon, op_point, op_question, op_another_null,

    op_left_paren, op_right_paren, op_not, op_tilde,
    op_pre_plus_plus, op_post_plus_plus,
    op_pre_minus_minus, op_post_minus_minus,
    op_star, op_print, op_random, op_attr, op_colon_question,
    op_point_question,
    op_pre_plus, op_pre_minus, op_a_circle, op_dollar, op_pre_null,
    op_post_null
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
    clear_input_line(true);
    if (err_num == NO_MEM_ERROR)
        tty_message("#out of memory\n");
    else
        tty_message("#error: %s.\n", error_msg[err_num]);
}

static bool push_op(operator *op)
{
    if (stk.curr_op < MAX_STACK - 1) {
        stk.op[++stk.curr_op] = *op;
        return true;
    } else {
        error = STACK_OV_ERROR;
        return false;
    }
}

static bool pop_op(operator *op)
{
    if (stk.curr_op >= 0) {
        *op = stk.op[stk.curr_op--];
        return true;
    } else {
        error = STACK_UND_ERROR;
        return false;
    }
}

static bool push_obj(object *obj)
{
    if (stk.curr_obj < MAX_STACK - 1) {
        ++stk.curr_obj;
        memmove(stk.obj + stk.curr_obj, obj, sizeof(object));
        return true;
    } else {
        error = STACK_OV_ERROR;
        return false;
    }
}

static bool pop_obj(object *obj)
{
    if (stk.curr_obj >= 0) {
        memmove(obj, stk.obj + stk.curr_obj, sizeof(object));
        --stk.curr_obj;
        return true;
    } else {
        error = STACK_UND_ERROR;
        return false;
    }
}

static bool check_operator(char side, operator *op, int mindepth)
{
    char c = *line;
    if (c == '\0' || c == CMDSEP) {
        *op = (side == BINARY ? op_null
               : side == LEFT ? op_pre_null
               : op_post_null);
        return true;
    }

    char d = line[1];
    if ((c == '$' || c == '@') && d != '\0'
        && (isalpha((unsigned char)d) || d == '_'))
        return false;           /* Danger! found named variable */

    if (side == LEFT && c == '(') {
        ++line;
        ++depth;
        *op = op_left_paren;
        return true;
    }

    if (side == RIGHT && c == ')') {
        if (--depth >= mindepth) {
            ++line;
            *op = op_right_paren;
        } else {
            /* exit without touching the parenthesis */
            *op = op_post_null;
        }
        return true;
    }

    if (side == RIGHT && (c == '}' || c == ']') && depth == mindepth) {
        /* allow also exiting with a '}' or a ']' */
        --depth;
        *op = op_post_null;
        return true;
    }

    int max = 0;
    operator match = op_null;
    for (int i = side == BINARY ? 1 : LOWEST_UNARY_CODE;
         *op_list[i].name != '\0'; ++i) {
        const char *name = op_list[i].name;
        int len = strlen(name);
        if (len > max && (side == BINARY || side == op_list[i].syntax)
            && strncmp(line, name, (size_t)len) == 0) {
            match = op_list[i].code;
            max = len;
        }
    }

    if (match != op_null) {
        *op = match;
        line += max;
        return true;
    }

    *op = (side == BINARY ? op_null
           : side == PRE_UNARY ? op_pre_null
           : op_post_null);
    if (side == BINARY)
        error = NO_OPERATOR_ERROR;
    return false;
}

/* the value of a digit of a number, or LONG_MAX if c is not one */
static long digit_value(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'z')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'Z')
        return c - 'A' + 10;
    return LONG_MAX;
}

static bool check_object(object *obj)
{
    bool found = false;
    char c = *line;

    if (c == '#' || isdigit((unsigned char)c)) {
        long num = 0, base = 10;
        while (c == '#' || isalnum((unsigned char)c)) {
            if (c == '#') {
                base = num == 0 ? 16 : num;
                num = 0;
            } else {
                long digit = digit_value(c);
                if (digit >= base) {
                    error = OUT_BASE_ERROR;
                    return false;
                }
                if (num > (LONG_MAX - digit) / base) {
                    error = OUT_RANGE_ERROR;
                    return false;
                }
                num = num * base + digit;
            }
            ++line;
            c = *line;
        }
        obj->type = TYPE_NUM;
        obj->num = num;
        found = true;
    } else if (c == '\"') {
        ++line;
        char *end = first_valid(line, '\"');
        if (*end != '\0') {
            obj->type = TYPE_TXT;
            obj->txt = ptrmcpy(obj->txt, line, end - line);
            if (!REAL_ERROR) {
                ptrunescape(obj->txt);
                found = true;
                line = end + 1;
            }
        }
    } else if ((c == '$' || c == '@') && line[1] != '\0'
               && (isalpha((unsigned char)line[1]) || line[1] == '_')) {
        /* found named variable */
        enum var_kind kind;
        if (*line == '@') {
            kind = VAR_NUM;
            obj->type = TYPE_NUM_VAR;
        } else {
            kind = VAR_TEXT;
            obj->type = TYPE_TXT_VAR;
        }
        ++line;
        char *end = line + 1;
        while (*end != '\0'
               && (isalpha((unsigned char)*end) || *end == '_'
                   || isdigit((unsigned char)*end)))
            ++end;
        c = *end;
        *end = '\0';
        varnode *named_var = *lookup_varnode(line, kind);
        if (named_var == NULL) {
            named_var = add_varnode(line, kind);
            if (named_var != NULL && opt_info)
                message_printf("#new variable: %s\n", line - 1);
        }
        *end = c;
        if (named_var == NULL)
            return false;
        line = end;
        if (kind == VAR_TEXT)
            obj->txt_ref = &named_var->str;
        else
            obj->num_ref = &named_var->num;
        found = true;
    } else if (strncmp(line, "timer", 5) == 0) {
        obj->type = TYPE_NUM;
        update_now();
        obj->num = diff_vtime(&now, &ref_time);
        line += 5;
        found = true;
    } else if (strncmp(line, "map", 3) == 0) {
        char buf[MAX_MAPLEN + 1];
        map_sprintf(buf);
        obj->type = TYPE_TXT;
        obj->txt = ptrmcpy(obj->txt, buf, strlen(buf));
        if (!REAL_ERROR) {
            line += 3;
            found = true;
        }
    } else if (strncmp(line, "noattr", 6) == 0) {
        obj->type = TYPE_TXT;
        obj->txt = ptrmcpy(obj->txt, tty_modestandoff,
                           strlen(tty_modestandoff));
        obj->txt = ptrmcat(obj->txt, tty_modenorm, strlen(tty_modenorm));
        if (!REAL_ERROR) {
            line += 6;
            found = true;
        }
    } else {
        error = NO_VALUE_ERROR;
    }

    return found;
}

static void check_delete(object *obj)
{
    if (obj->type == TYPE_TXT && obj->txt != NULL) {
        ptrdel(&obj->txt);
        obj->txt = NULL;
    }
}

/*
 * add, subtract and multiply, wrapping around on overflow as in two's
 * complement
 */
static long wrap_add(long a, long b)
{
    return (long)((unsigned long)a + (unsigned long)b);
}

static long wrap_sub(long a, long b)
{
    return (long)((unsigned long)a - (unsigned long)b);
}

static long wrap_mul(long a, long b)
{
    return (long)((unsigned long)a * (unsigned long)b);
}

/*
 * shift *num left, or else right, by count bits: the bits shifted out are
 * lost, and a right shift keeps the sign; a negative count is an error
 */
static void shift(long *num, long count, bool left)
{
    const long bits = sizeof(long) * CHAR_BIT;
    if (count < 0)
        error = OUT_RANGE_ERROR;
    else if (left)
        *num = count >= bits ? 0 : (long)((unsigned long)*num << count);
    else if (*num >= 0)
        *num = count >= bits ? 0 : *num >> count;
    else
        *num = count >= bits ? -1 : ~(~*num >> count);
}

/*
 * divide *num by divisor, or else take the remainder, which is never
 * negative; dividing by zero is an error
 */
static void divide(long *num, long divisor, bool remainder)
{
    if (divisor == 0) {
        error = DIV_BY_ZERO_ERROR;
    } else if (divisor == -1) {
        /* LONG_MIN / -1 overflows, and wraps around to LONG_MIN */
        *num = remainder ? 0 : (long)(0UL - (unsigned long)*num);
    } else if (!remainder) {
        *num /= divisor;
    } else {
        *num %= divisor;
        /* |*num| < |divisor|, so this does not overflow */
        if (*num < 0)
            *num = divisor < 0 ? *num - divisor : *num + divisor;
    }
}

/*
 * repeat the text of dst n times, for n > 1; a result too long for a
 * text is an error
 */
static ptr *repeat_text(ptr *dst, long n)
{
    long delta = ptrlen(dst);
    if (delta == 0)
        return dst;
    if (n > INT_MAX / delta) {
        error = limit_mem != 0 ? MEM_LIMIT_ERROR : OUT_RANGE_ERROR;
        return dst;
    }
    dst = ptrsetlen(dst, (int)(delta * n));
    if (REAL_ERROR)
        return dst;
    char *tmp = ptrdata(dst);
    for (long i = 1; i < n; ++i)
        memcpy(tmp + i * delta, tmp, delta);
    return dst;
}

static bool exe_op(operator *op)
{
    object o1 = { 0 };
    object o2 = { 0 };
    object *p = NULL;
    bool ret = false;

    switch (*op) {
    case op_comma:
        if (!(pop_obj(&o2) && pop_obj(&o1)) && REAL_ERROR)
            break;
        check_delete(&o1);
        p = &o2;
        ret = true;
        break;
    case op_eq:
        if (!(pop_obj(&o2) && pop_obj(&o1)) && REAL_ERROR)
            break;

        if (o2.type == TYPE_NUM_VAR) {
            o2.num = *o2.num_ref;
            o2.type = TYPE_NUM;
        }

        if (o1.type == TYPE_NUM_VAR && o2.type == TYPE_NUM) {
            *o1.num_ref = o2.num;
            p = &o2;
            ret = true;
        } else if (o1.type == TYPE_TXT_VAR
                   && (o2.type == TYPE_TXT || o2.type == TYPE_TXT_VAR)) {
            if (o2.type == TYPE_TXT_VAR) {
                o2.txt = ptrdup(*o2.txt_ref);
                if (REAL_ERROR)
                    break;
                o2.type = TYPE_TXT;
            }

            *o1.txt_ref = ptrcpy(*o1.txt_ref, o2.txt);
            if (REAL_ERROR)
                break;
            p = &o2;
            ret = true;
        } else {
            error = SYNTAX_ERROR;
        }
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
        if (!(pop_obj(&o2) && pop_obj(&o1)) && REAL_ERROR)
            break;

        if (o2.type == TYPE_NUM_VAR) {
            o2.num = *o2.num_ref;
            o2.type = TYPE_NUM;
        }

        if (o1.type == TYPE_NUM_VAR && o2.type == TYPE_NUM) {
            long *l = o1.num_ref;

            switch (*op) {
            case op_or_or_eq:
                if (o2.num != 0)
                    *l = 1;
                else
                    *l = *l != 0;
                break;
            case op_xor_xor_eq:
                if (o2.num != 0)
                    *l = *l == 0;
                else
                    *l = *l != 0;
                break;
            case op_and_and_eq:
                if (o2.num == 0)
                    *l = 0;
                else
                    *l = *l != 0;
                break;
            case op_or_eq:
                *l |= o2.num;
                break;
            case op_xor_eq:
                *l ^= o2.num;
                break;
            case op_and_eq:
                *l &= o2.num;
                break;
            case op_lshift_eq:
            case op_rshift_eq:
                shift(l, o2.num, *op == op_lshift_eq);
                break;
            case op_plus_eq:
                *l = wrap_add(*l, o2.num);
                break;
            case op_minus_eq:
                *l = wrap_sub(*l, o2.num);
                break;
            case op_times_eq:
                *l = wrap_mul(*l, o2.num);
                break;
            case op_div_eq:
            case op_ampersand_eq:
                divide(l, o2.num, *op == op_ampersand_eq);
                break;
            default:
                break;
            }
            o2.num = *l;
            p = &o2;
            ret = true;
        } else if (*op == op_plus_eq && o1.type == TYPE_TXT_VAR
                   && (o2.type == TYPE_TXT || o2.type == TYPE_TXT_VAR)) {
            ptr *src = o2.type == TYPE_TXT ? o2.txt : *o2.txt_ref;

            *o1.txt_ref = ptrcat(*o1.txt_ref, src);
            check_delete(&o2);

            ptr *dst = ptrdup(*o1.txt_ref);
            if (REAL_ERROR)
                break;

            o1.type = TYPE_TXT;
            o1.txt = dst;
            p = &o1;
            ret = true;
        } else if (*op == op_times_eq && o1.type == TYPE_TXT_VAR
                   && (o2.type == TYPE_NUM || o2.type == TYPE_NUM_VAR)) {
            if (o2.type == TYPE_NUM_VAR) {
                o2.num = *o2.num_ref;
                o2.type = TYPE_NUM;
            }

            if (o2.num < 0) {
                error = OUT_RANGE_ERROR;
            } else if (o2.num == 0) {
                ptrzero(*o1.txt_ref);
            } else if (o2.num > 1 && *o1.txt_ref != NULL) {
                *o1.txt_ref = repeat_text(*o1.txt_ref, o2.num);
            }

            check_delete(&o2);
            ptr *dst = ptrdup(*o1.txt_ref);
            if (REAL_ERROR)
                break;

            o1.type = TYPE_TXT;
            o1.txt = dst;
            p = &o1;
            ret = true;
        } else {
            error = SYNTAX_ERROR;
        }
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
        if (!(pop_obj(&o2) && pop_obj(&o1)) && REAL_ERROR)
            break;

        if (o1.type == TYPE_NUM_VAR) {
            o1.num = *o1.num_ref;
            o1.type = TYPE_NUM;
        }
        if (o2.type == TYPE_NUM_VAR) {
            o2.num = *o2.num_ref;
            o2.type = TYPE_NUM;
        }

        if (o1.type == TYPE_NUM && o2.type == TYPE_NUM) {
            switch (*op) {
            case op_less:
                o1.num = o1.num < o2.num ? 1 : 0;
                break;
            case op_less_eq:
                o1.num = o1.num <= o2.num ? 1 : 0;
                break;
            case op_greater:
                o1.num = o1.num > o2.num ? 1 : 0;
                break;
            case op_greater_eq:
                o1.num = o1.num >= o2.num ? 1 : 0;
                break;
            case op_eq_eq:
                o1.num = o1.num == o2.num ? 1 : 0;
                break;
            case op_not_eq:
                o1.num = o1.num != o2.num ? 1 : 0;
                break;
            case op_or_or:
                o1.num = o1.num != 0 || o2.num != 0;
                break;
            case op_xor_xor:
                o1.num = (o1.num != 0) != (o2.num != 0);
                break;
            case op_and_and:
                o1.num = o1.num != 0 && o2.num != 0;
                break;
            case op_or:
                o1.num |= o2.num;
                break;
            case op_xor:
                o1.num ^= o2.num;
                break;
            case op_and:
                o1.num &= o2.num;
                break;
            case op_lshift:
            case op_rshift:
                shift(&o1.num, o2.num, *op == op_lshift);
                break;
            case op_minus:
                o1.num = wrap_sub(o1.num, o2.num);
                break;
            case op_plus:
                o1.num = wrap_add(o1.num, o2.num);
                break;
            case op_times:
                o1.num = wrap_mul(o1.num, o2.num);
                break;
            case op_division:
            case op_ampersand:
                divide(&o1.num, o2.num, *op == op_ampersand);
                break;
            default:
                break;
            }

            p = &o1;
            ret = true;
        } else if ((o1.type == TYPE_TXT || o1.type == TYPE_TXT_VAR)
                   && (o2.type == TYPE_TXT || o2.type == TYPE_TXT_VAR)) {
            if (o1.type == TYPE_TXT_VAR) {
                o1.txt = ptrdup(*o1.txt_ref);
                o1.type = TYPE_TXT; /* not a var anymore */
                if (REAL_ERROR)
                    break;
            }
            ptr *dst = o1.txt;
            ptr *src = o2.type == TYPE_TXT ? o2.txt : *o2.txt_ref;

            if (*op == op_plus) {
                dst = ptrcat(dst, src);
                o1.type = TYPE_TXT;
            } else {
                o1.num = ptrcmp(dst, src);
                switch (*op) {
                case op_minus:
                    break;
                case op_less:
                    o1.num = o1.num < 0;
                    break;
                case op_less_eq:
                    o1.num = o1.num <= 0;
                    break;
                case op_greater:
                    o1.num = o1.num > 0;
                    break;
                case op_greater_eq:
                    o1.num = o1.num >= 0;
                    break;
                case op_eq_eq:
                    o1.num = o1.num == 0;
                    break;
                case op_not_eq:
                    o1.num = o1.num != 0;
                    break;
                default:
                    error = SYNTAX_ERROR;
                    p = NULL;
                    ret = false;
                    break;
                }
                check_delete(&o1);
                /* only after check_delete(&o1), so that it frees dst */
                o1.type = TYPE_NUM;
            }
            check_delete(&o2);
            if (!REAL_ERROR) {
                o1.txt = dst;
                p = &o1;
                ret = true;
            }
        } else if (*op == op_times
                   && (o1.type == TYPE_TXT_VAR || o1.type == TYPE_TXT)
                   && o2.type == TYPE_NUM) {
            if (o1.type == TYPE_TXT_VAR) {
                /* the result is a copy, not the text of the variable */
                o1.txt = o2.num > 0 ? ptrdup(*o1.txt_ref) : NULL;
                o1.type = TYPE_TXT;
                if (REAL_ERROR)
                    break;
            }
            ptr *dst = o1.txt;

            if (o2.num < 0) {
                error = OUT_RANGE_ERROR;
            } else if (o2.num == 0) {
                ptrzero(dst);
            } else if (o2.num > 1 && dst != NULL) {
                dst = repeat_text(dst, o2.num);
            }
            check_delete(&o2);
            if (REAL_ERROR)
                break;

            o1.type = TYPE_TXT;
            o1.txt = dst;
            p = &o1;
            ret = true;
        } else {
            error = SYNTAX_ERROR;
        }
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
    case op_point: {
        if (!(pop_obj(&o2) && pop_obj(&o1)) && REAL_ERROR)
            break;

        if (o2.type == TYPE_NUM_VAR) {
            o2.num = *o2.num_ref;
            o2.type = TYPE_NUM;
        }

        if ((o1.type != TYPE_TXT_VAR && o1.type != TYPE_TXT)
            || o2.type != TYPE_NUM) {
            error = SYNTAX_ERROR;
            break;
        }

        if (o2.num <= 0) {
            error = OUT_RANGE_ERROR;
            break;
        }

        ptr *start;
        ptr *dst;
        bool danger = false;
        if (o1.type == TYPE_TXT_VAR) {
            o1.type = TYPE_TXT;
            o1.txt = NULL;
            dst = NULL;
            start = *o1.txt_ref;
        } else {
            /* Potentially dangerous: src and dst are overlapping */
            start = o1.txt;
            dst = start;
            danger = true;
        }
        ptr *src = start;

        if (src == NULL) {
            /* src == empty string. just return it */
            check_delete(&o2);
            o1.txt = src;
            if (!REAL_ERROR) {
                p = &o1;
                ret = true;
            }
            break;
        }

        int srclen = ptrlen(src);
        char *ssrc = ptrdata(src);
        /* the point operators count characters, not bytes */
        int nchars = utf8_count(ssrc, srclen);
#define BYTES(n) ((int)utf8_offset(ssrc, srclen, (n)))

        switch (*op) {
        case op_colon_less: {
            int j = 0;
            while (o2.num != 0 && srclen != 0) {
                /* skip span of multiple word delimeters */
                while (srclen != 0 && IS_DELIM(*ssrc)) {
                    --srclen;
                    ++ssrc;
                    ++j;
                }
                /* skip whole words */
                if (srclen == 0)
                    break;
                char *tmp = memchrs(ssrc, srclen, DELIM, DELIM_LEN);
                if (tmp == NULL)
                    break;
                int i = tmp - ssrc;
                --o2.num;
                ssrc += i;
                j += i;
                srclen -= i;
            }

            if (o2.num != 0) {
                /* end of valid string before the n-th word */
                if (!danger)
                    dst = ptrcpy(dst, start);
            } else if (danger) {
                ptrtrunc(dst, j);
            } else {
                dst = ptrmcpy(dst, ptrdata(start), j);
            }
            break;
        }
        case op_colon:
        case op_colon_greater:
            --o2.num;
            /* skip span of multiple word delimeters */
            while (srclen != 0 && IS_DELIM(*ssrc)) {
                --srclen;
                ++ssrc;
            }
            while (o2.num != 0 && srclen != 0) {
                /* skip whole words */
                char *tmp = memchrs(ssrc, srclen, DELIM, DELIM_LEN);
                if (tmp == NULL)
                    break;
                int i = tmp - ssrc;
                --o2.num;
                ssrc += i;
                srclen -= i;
                /* skip span of multiple word delimeters */
                while (srclen != 0 && IS_DELIM(*ssrc)) {
                    --srclen;
                    ++ssrc;
                }
            }

            if (o2.num != 0) {
                /* end of valid string before the n-th word */
                ptrzero(dst);
            } else {
                char *tmp = NULL;
                if (*op == op_colon)
                    tmp = memchrs(ssrc, srclen, DELIM, DELIM_LEN);
                if (tmp != NULL)
                    dst = ptrmcpy(dst, ssrc, tmp - ssrc);
                else
                    dst = ptrmcpy(dst, ssrc, srclen);
            }
            break;
        case op_less_colon:
            --o2.num;
            while (o2.num != 0 && srclen != 0) {
                /* skip span of multiple word delimeters */
                while (srclen != 0 && IS_DELIM(ssrc[srclen - 1]))
                    --srclen;
                /* skip whole words */
                if (srclen == 0)
                    break;
                char *tmp = memrchrs(ssrc, srclen, DELIM, DELIM_LEN);
                if (tmp == NULL)
                    break;
                --o2.num;
                srclen = tmp - ssrc;
            }

            if (o2.num != 0) /* end of valid string before the n-th word */
                ptrzero(dst);
            else
                dst = ptrmcpy(dst, ssrc, srclen);
            break;
        case op_greater_colon:
            while (o2.num != 0 && srclen != 0) {
                /* skip span of multiple word delimeters */
                while (srclen != 0 && IS_DELIM(ssrc[srclen - 1]))
                    --srclen;
                /* skip whole words */
                if (srclen == 0)
                    break;
                char *tmp = memrchrs(ssrc, srclen, DELIM, DELIM_LEN);
                if (tmp == NULL)
                    break;
                --o2.num;
                srclen = tmp - ssrc;
            }

            if (o2.num != 0) /* end of valid string before the n-th word */
                dst = ptrcpy(dst, start);
            else
                dst = ptrmcpy(dst, ssrc + srclen + 1,
                              (ptrlen(start)
                               - (ssrc + srclen + 1 - ptrdata(start))));
            break;
        case op_point: {
            int j = BYTES(o2.num <= nchars ? o2.num - 1 : nchars);
            dst = ptrmcpy(dst, ssrc + j,
                          BYTES(o2.num <= nchars ? o2.num : nchars) - j);
            break;
        }
        case op_point_less: {
            int j = BYTES(o2.num < nchars ? o2.num : nchars);
            if (danger)
                ptrtrunc(dst, j);
            else
                dst = ptrmcpy(dst, ssrc, j);
            break;
        }
        case op_less_point: {
            int j = nchars - o2.num + 1;
            j = j < 0 ? 0 : BYTES(j);
            if (danger)
                ptrtrunc(dst, j);
            else
                dst = ptrmcpy(dst, ssrc, j);
            break;
        }
        case op_point_greater: {
            int j = BYTES(o2.num - 1 < nchars ? o2.num - 1 : nchars);
            dst = ptrmcpy(dst, ssrc + j, srclen - j);
            break;
        }
        case op_greater_point: {
            int j = nchars - o2.num;
            j = j < 0 ? 0 : BYTES(j);
            dst = ptrmcpy(dst, ssrc + j, srclen - j);
            break;
        }
        default:
            break;
        }
#undef BYTES
        check_delete(&o2);
        o1.txt = dst;
        if (!REAL_ERROR) {
            p = &o1;
            ret = true;
        }
        break;
    }
    case op_colon_question:
    case op_point_question: {
        if (!pop_obj(&o1) && REAL_ERROR)
            break;

        ptr *src;
        if (o1.type == TYPE_TXT) {
            src = o1.txt;
        } else if (o1.type == TYPE_TXT_VAR) {
            src = *o1.txt_ref;
        } else {
            error = SYNTAX_ERROR;
            break;
        }
        if (src == NULL) {
            /* empty string. return 0 */
            check_delete(&o1);
            o1.type = TYPE_NUM;
            o1.num = 0;
            p = &o1;
            ret = true;
            break;
        }

        char *ssrc = ptrdata(src);
        int srclen = ptrlen(src);

        if (*op == op_colon_question) {
            o1.num = 0;
            /* skip span of multiple word delimeters */
            while (srclen != 0 && IS_DELIM(*ssrc)) {
                ++ssrc;
                --srclen;
            }
            while (srclen != 0) {
                /* skip whole words */
                char *tmp = memchrs(ssrc, srclen, DELIM, DELIM_LEN);
                if (tmp != NULL) {
                    int i = tmp - ssrc;
                    ++o1.num;
                    ssrc += i;
                    srclen -= i;
                    /* skip span of multiple word delimeters */
                    while (srclen != 0 && IS_DELIM(*ssrc)) {
                        --srclen;
                        ++ssrc;
                    }
                } else {
                    srclen = 0;
                    ++o1.num;
                }
            }
        } else {
            o1.num = utf8_count(ssrc, srclen);
        }

        check_delete(&o1);
        o1.type = TYPE_NUM;
        p = &o1;
        ret = true;
        break;
    }
    case op_question: {
        if (!(pop_obj(&o2) && pop_obj(&o1)) && REAL_ERROR)
            break;

        ptr *src = NULL;
        if (o1.type == TYPE_TXT)
            src = o1.txt;
        else if (o1.type == TYPE_TXT_VAR)
            src = *o1.txt_ref;
        else
            error = SYNTAX_ERROR;

        ptr *dst = NULL;
        if (o2.type == TYPE_TXT)
            dst = o2.txt;
        else if (o2.type == TYPE_TXT_VAR)
            dst = *o2.txt_ref;
        else
            error = SYNTAX_ERROR;

        if (error != 0)
            break;
        char *found = ptrfind(src, dst);
        int i = 0;
        if (found != NULL)
            i = (int)utf8_count(ptrdata(src), found - ptrdata(src)) + 1;
        check_delete(&o1);
        check_delete(&o2);
        o1.type = TYPE_NUM;
        o1.num = i;
        p = &o1;
        ret = true;
        break;
    }
    case op_null:
    case op_another_null:
        if (!(pop_obj(&o2) && pop_obj(&o1)) && REAL_ERROR)
            break;

        check_delete(&o1);
        check_delete(&o2);

        o1.type = 0;
        o1.num = 0;
        o1.txt = NULL;

        p = &o1;
        ret = true;
        break;
    case op_left_paren:
        error = MISSING_PAREN_ERROR;
        break;
    case op_right_paren:
        if (!pop_op(op) && REAL_ERROR)
            break;

        if (*op != op_left_paren)
            error = MISMATCH_PAREN_ERROR;
        else
            ret = true;

        break;
    case op_random:
    case op_pre_plus:
    case op_pre_minus:
    case op_not:
    case op_tilde:
        if (!pop_obj(&o1) && REAL_ERROR)
            break;

        if (o1.type == TYPE_NUM_VAR) {
            o1.num = *o1.num_ref;
            o1.type = TYPE_NUM;
        }
        if (o1.type == TYPE_NUM) {
            if (*op == op_pre_minus) {
                o1.num = wrap_sub(0, o1.num);
            } else if (*op == op_not) {
                o1.num = o1.num == 0;
            } else if (*op == op_tilde) {
                o1.num = ~o1.num;
            } else if (*op == op_random) {
                if (o1.num <= 0) {
                    error = OUT_RANGE_ERROR;
                    break;
                }
                o1.num = random_below(o1.num);
            }
            p = &o1;
            ret = true;
        } else {
            error = SYNTAX_ERROR;
        }
        break;
    case op_attr:
        if (!pop_obj(&o1) && REAL_ERROR)
            break;

        if (o1.type == TYPE_TXT_VAR) {
            o1.txt = ptrdup(*o1.txt_ref);
            if (REAL_ERROR)
                break;
            o1.type = TYPE_TXT;
        }

        if (o1.type == TYPE_TXT) {
            char dummy[ATTR_SGR_MAX]; /* attr_string must write somewhere */
            struct attr attr = { 0 };

            if (o1.txt != NULL && !attr_parse(ptrdata(o1.txt), &attr)) {
                error = BAD_ATTR_ERROR;
            } else {
                o1.txt = ptrsetlen(o1.txt, ATTR_SGR_MAX);
                if (REAL_ERROR)
                    break;
                attr_string(&attr, ptrdata(o1.txt), dummy);
                ptrtrunc(o1.txt, strlen(ptrdata(o1.txt)));
                p = &o1;
                ret = true;
            }
        } else {
            error = NO_STRING_ERROR;
        }
        break;

    case op_star:
    case op_print:
        if (!pop_obj(&o1) && REAL_ERROR)
            break;

        if (o1.type == TYPE_NUM_VAR)
            o1.num = *o1.num_ref;
        else if (o1.type == TYPE_TXT_VAR)
            o1.txt = *o1.txt_ref;

        if (o1.type == TYPE_NUM || o1.type == TYPE_NUM_VAR) {
            o1.txt = NULL;
            if (*op == op_print) {
                char buf[LONGLEN];
                sprintf(buf, "%ld", o1.num);
                o1.txt = ptrmcpy(o1.txt, buf, strlen(buf));
            } else {
                /* character with that code point */
                char buf[UTF8_MAX];
                uint32_t cp = (o1.num < 0 || o1.num > UNICODE_MAX
                               ? UNICODE_REPLACEMENT
                               : (uint32_t)o1.num);
                o1.txt = ptrmcpy(o1.txt, buf, utf8_put(cp, buf));
            }
            if (REAL_ERROR)
                break;
            o1.type = TYPE_TXT;
            p = &o1;
            ret = true;
        } else if (o1.type == TYPE_TXT || o1.type == TYPE_TXT_VAR) {
            if (*op == op_print) {
                o1.num = 0;
                if (o1.txt != NULL && ptrlen(o1.txt) != 0) {
                    errno = 0;
                    o1.num = strtol(ptrdata(o1.txt), NULL, 10);
                    if (errno == ERANGE)
                        error = OUT_RANGE_ERROR;
                }
            } else {
                if (o1.txt != NULL && ptrlen(o1.txt) != 0) {
                    /* code point of the first character */
                    uint32_t cp;
                    utf8_get(ptrdata(o1.txt), ptrlen(o1.txt), &cp);
                    o1.num = cp;
                } else {
                    o1.num = 0;
                }
            }
            check_delete(&o1);
            if (REAL_ERROR)
                break;
            o1.type = TYPE_NUM;
            p = &o1;
            ret = true;
        } else {
            error = SYNTAX_ERROR;
        }
        break;
    case op_pre_plus_plus:
    case op_post_plus_plus:
    case op_pre_minus_minus:
    case op_post_minus_minus:
        if (!pop_obj(&o1) && REAL_ERROR)
            break;

        if (o1.type == TYPE_NUM_VAR) {
            long *l = o1.num_ref;
            o1.type = TYPE_NUM;

            if (*op == op_pre_plus_plus) {
                *l = wrap_add(*l, 1);
                o1.num = *l;
            } else if (*op == op_post_plus_plus) {
                o1.num = *l;
                *l = wrap_add(*l, 1);
            } else if (*op == op_pre_minus_minus) {
                *l = wrap_sub(*l, 1);
                o1.num = *l;
            } else {
                o1.num = *l;
                *l = wrap_sub(*l, 1);
            }

            p = &o1;
            ret = true;
        } else {
            error = SYNTAX_ERROR;
        }
        break;
    case op_a_circle:
    case op_dollar: {
        if (!pop_obj(&o1) && REAL_ERROR)
            break;

        enum var_kind kind = *op == op_dollar ? VAR_TEXT : VAR_NUM;

        if (o1.type == TYPE_NUM_VAR) {
            o1.type = TYPE_NUM;
            o1.num = *o1.num_ref;
        }

        if (o1.type == TYPE_NUM) {
            if (o1.num < -GLOBAL_VAR_COUNT || o1.num >= PARAM_COUNT) {
                error = OUT_RANGE_ERROR;
                break;
            }
            if (kind == VAR_TEXT) {
                o1.type = TYPE_TXT_VAR;
                o1.txt_ref = text_var(o1.num);
            } else {
                o1.type = TYPE_NUM_VAR;
                o1.num_ref = num_var(o1.num);
            }
            p = &o1;
            ret = true;
        } else {
            if (o1.type == TYPE_TXT_VAR) {
                o1.txt = *o1.txt_ref;
            } else if (o1.type != TYPE_TXT) {
                error = SYNTAX_ERROR;
                break;
            }

            if (o1.txt == NULL) {
                error = INVALID_NAME_ERROR;
                break;
            }
            char *tmp = ptrdata(o1.txt);
            if (*tmp == '_' || isalpha((unsigned char)*tmp)) {
                ++tmp;
                while (*tmp == '_' || isalnum((unsigned char)*tmp))
                    ++tmp;
            }
            if (*tmp != '\0') {
                error = INVALID_NAME_ERROR;
                break;
            }

            varnode *named_var = *lookup_varnode(ptrdata(o1.txt), kind);
            if (named_var == NULL) {
                named_var = add_varnode(ptrdata(o1.txt), kind);
                if (named_var == NULL)
                    break;
                if (opt_info) {
                    message_printf("#new variable: %c%s\n",
                                   kind == VAR_TEXT ? '$' : '@',
                                   ptrdata(o1.txt));
                }
            }
            check_delete(&o1);
            if (kind == VAR_TEXT) {
                o1.type = TYPE_TXT_VAR;
                o1.txt_ref = &named_var->str;
            } else {
                o1.type = TYPE_NUM_VAR;
                o1.num_ref = &named_var->num;
            }
            p = &o1;
            ret = true;
        }
        break;
    }
    case op_pre_null:
    case op_post_null:
        ret = true;
        break;
    default:
        break;
    }

    if (REAL_ERROR) {
        check_delete(&o2);
        check_delete(&o1);
    }

    if (!REAL_ERROR) {
        if (!ret) {
            error = NOT_DONE_ERROR;
        } else if (p != NULL) {
            /* txt is the value of TYPE_TXT only; elsewhere it may be stale */
            if (p->type != TYPE_TXT)
                p->txt = NULL;
            if (!push_obj(p))
                check_delete(p);
        }
    }

    if (REAL_ERROR)
        return false;

    return ret;
}

static int whichfirst(operator *op1, operator *op2)
{
    int p1 = op_list[*op1].priority;
    int p2 = op_list[*op2].priority;
    if (p1 != p2)
        return p1 > p2 ? -1 : 1;

    return op_list[*op1].assoc == LEFT ? -1 : 1;
}

static bool compare_and_unload(operator *op)
{
    if (REAL_ERROR || stk.curr_op < 0)
        return true;

    /* execute the operators on the stack that go before op */
    bool put_back = false;
    operator new = op_null;
    while (stk.curr_op >= 0 && pop_op(&new) && !REAL_ERROR) {
        if (whichfirst(&new, op) != -1) {
            put_back = true;
            break;
        }
        if (!exe_op(&new))
            break;
    }

    if (REAL_ERROR)
        return false;
    if (!put_back)
        return true;
    return push_op(&new);
}

static bool _eval(int mindepth)
{
    bool endreached = false;

    for (;;) {
        object obj = { 0 };
        operator op = op_null;

        while (*line == ' ')
            ++line;
        if (*line == '\0' || *line == CMDSEP)
            endreached = true;

        while (check_operator(LEFT, &op, mindepth) && push_op(&op)
               && !endreached) {
            if (error != 0)
                return false;
            while (*line == ' ')
                ++line;
            if (*line == '\0' || *line == CMDSEP)
                endreached = true;
        }
        if (error != 0)
            return false;

        if (!endreached && check_object(&obj) && !push_obj(&obj))
            check_delete(&obj);
        if (error != 0)
            return false;

        while (*line == ' ')
            ++line;
        if (*line == '\0' || *line == CMDSEP)
            endreached = true;

        while (check_operator(RIGHT, &op, mindepth) && compare_and_unload(&op)
               && exe_op(&op) && depth >= mindepth && !endreached) {
            if (error != 0)
                return false;
            while (*line == ' ')
                ++line;
            if (*line == '\0' || *line == CMDSEP)
                endreached = true;
        }
        if (error != 0)
            return false;

        if (endreached || depth < mindepth)
            break;

        if (!(check_operator(BINARY, &op, mindepth) && compare_and_unload(&op)
              && push_op(&op))
            && error != 0)
            return false;
    }
    return true;
}

int eval_any(long *lres, ptr **pres, char **what)
{
    int printmode;
    if (pres != NULL)
        printmode = PRINT_AS_PTR;
    else if (lres != NULL)
        printmode = PRINT_AS_LONG;
    else
        printmode = PRINT_NOTHING;

    error = 0;
    stk.curr_obj = -1;
    stk.curr_op = -1;
    line = *what;

    depth = 0;
    _eval(0);

    /* an empty expression, such as in "#(" */
    if (error == 0 && stk.curr_obj < 0)
        error = NO_VALUE_ERROR;
    object res = { 0 };
    if (error == 0)
        pop_obj(&res);
    bool handed_over = false;   /* res.txt is now *pres */
    if (error != 0) {
        if (opt_debug)
            message_printf("#result not available\n");
    } else if (printmode != PRINT_NOTHING || opt_debug) {
        if (res.type == TYPE_NUM || res.type == TYPE_NUM_VAR) {
            long val = res.type == TYPE_NUM ? res.num : *res.num_ref;

            if (printmode == PRINT_AS_PTR) {
                *pres = ptrsetlen(*pres, LONGLEN);
                if (!MEM_ERROR) {
                    sprintf(ptrdata(*pres), "%ld", val);
                    (*pres)->len = strlen(ptrdata(*pres));
                }
            } else if (printmode == PRINT_AS_LONG) {
                *lres = val;
            }

            if (opt_debug) {
                if (error != 0)
                    message_printf("#result not available\n");
                else
                    message_printf("#result: %ld\n", val);
            }
        } else {
            ptr *txt = res.type == TYPE_TXT ? res.txt : *res.txt_ref;
            if (printmode == PRINT_AS_PTR) {
                if (txt != NULL && *ptrdata(txt) != '\0') {
                    if (res.type == TYPE_TXT) {
                        ptrdel(pres);
                        *pres = txt; /* shortcut! */
                        handed_over = true;
                    } else {
                        *pres = ptrcpy(*pres, txt);
                    }
                } else {
                    ptrzero(*pres);
                }
            }
            if (opt_debug) {
                if (error != 0)
                    message_printf("#result not available\n");
                else if (txt != NULL && *ptrdata(txt) != '\0')
                    message_printf("#result: %s\n", ptrdata(txt));
                else
                    message_printf("#result empty\n");
            }
        }
    }
    *what = line;

    if (error == 0) {
        if (!handed_over)
            check_delete(&res);
    } else {
        while (stk.curr_obj >= 0) {
            pop_obj(&res);
            check_delete(&res);
        }
        res.type = 0;
    }

    if (res.type == TYPE_TXT_VAR)
        res.type = TYPE_TXT;
    else if (res.type == TYPE_NUM_VAR)
        res.type = TYPE_NUM;

    return res.type;
}

int evalp(ptr **res, char **what)
{
    return eval_any(NULL, res, what);
}

int evall(long *res, char **what)
{
    return eval_any(res, NULL, what);
}

int evaln(char **what)
{
    return eval_any(NULL, NULL, what);
}
