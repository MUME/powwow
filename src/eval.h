/* public things from eval.c */

#ifndef _EVAL_H_
#define _EVAL_H_

#include <stdbool.h>

/*
 * the errors of evaluating expressions and running commands, in 'error';
 * their numbers stay the same for plugins
 */
enum eval_error {
    NO_ERROR = 0,
    STACK_OV_ERROR = 1,
    STACK_UND_ERROR = 2,
    DYN_STACK_OV_ERROR = 3,
    DYN_STACK_UND_ERROR = 4,
    SYNTAX_ERROR = 5,
    NO_OPERATOR_ERROR = 6,
    NO_VALUE_ERROR = 7,
    DIV_BY_ZERO_ERROR = 8,
    OUT_RANGE_ERROR = 9,
    MISSING_PAREN_ERROR = 10,
    MISMATCH_PAREN_ERROR = 11,
    INTERNAL_ERROR = 12,
    NOT_SUPPORTED_ERROR = 13,
    NOT_DONE_ERROR = 14,
    /* 15 is unused */
    MEM_LIMIT_ERROR = 16,
    MAX_LOOP_ERROR = 17,
    NO_NUM_VALUE_ERROR = 18,
    NO_STRING_ERROR = 19,
    NO_LABEL_ERROR = 20,
    MISSING_SEPARATOR_ERROR = 21,
    HISTORY_RECURSION_ERROR = 22,
    USER_BREAK = 23,
    OUT_OF_VAR_SPACE_ERROR = 24,
    UNDEFINED_VARIABLE_ERROR = 25,
    OUT_BASE_ERROR = 26,
    BAD_ATTR_ERROR = 27,
    INVALID_NAME_ERROR = 28,
};

/* the type of the value of an expression, or EVAL_TYPE_NONE after an error */
enum eval_type {
    EVAL_TYPE_NONE = 0,
    EVAL_TYPE_NUM = 1,
    EVAL_TYPE_TXT = 2,
    EVAL_TYPE_NUM_VAR = 3,
    EVAL_TYPE_TXT_VAR = 4,
};

enum eval_type eval_any(long *lres, ptr **pres, char **what);
enum eval_type evalp(ptr **pres, char **what);
enum eval_type evall(long *lres, char **what);
enum eval_type evaln(char **what);

void print_error(enum eval_error err);

extern const char *const error_msg[];
extern enum eval_error error;

/* is there an error, other than the user interrupting a command? */
static inline bool real_error(void)
{
    return error != NO_ERROR && error != USER_BREAK;
}

/* did a text or string get longer than the limit? */
static inline bool mem_error(void)
{
    return error == MEM_LIMIT_ERROR;
}

#endif /* _EVAL_H_ */
