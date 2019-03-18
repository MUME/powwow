/* public things from eval.c */

#ifndef _EVAL_H_
#define _EVAL_H_

#define STACK_OV_ERROR        1
#define STACK_UND_ERROR	      2
#define DYN_STACK_OV_ERROR    3
#define DYN_STACK_UND_ERROR   4
#define SYNTAX_ERROR          5
#define NO_OPERATOR_ERROR     6
#define NO_VALUE_ERROR        7
#define DIV_BY_ZERO_ERROR     8
#define OUT_RANGE_ERROR       9
#define MISSING_PAREN_ERROR  10
#define MISMATCH_PAREN_ERROR 11
#define INTERNAL_ERROR	     12
#define NOT_SUPPORTED_ERROR  13
#define NOT_DONE_ERROR       14
#define NO_MEM_ERROR         15
#define MEM_LIMIT_ERROR      16
#define MAX_LOOP_ERROR       17
#define NO_NUM_VALUE_ERROR   18
#define NO_STRING_ERROR      19
#define NO_LABEL_ERROR       20
#define MISSING_SEPARATOR_ERROR  21
#define HISTORY_RECURSION_ERROR  22
#define USER_BREAK		 23
#define OUT_OF_VAR_SPACE_ERROR   24
#define UNDEFINED_VARIABLE_ERROR 25
#define OUT_BASE_ERROR		 26
#define BAD_ATTR_ERROR		 27
#define INVALID_NAME_ERROR	 28

#define TYPE_NUM 1
#define TYPE_TXT 2
#define TYPE_NUM_VAR 3
#define TYPE_TXT_VAR 4

#define PRINT_NOTHING 0
#define PRINT_AS_PTR  1
#define PRINT_AS_LONG 2

int  eval_any(long *lres, ptr *pres, char **what);
int  evalp(            ptr *pres, char **what);
int  evall(long *lres,            char **what);
int  evaln(                       char **what);

void print_error(int err_num);

extern char *error_msg[];
extern int error;

#define REAL_ERROR (error && error != USER_BREAK)
#define MEM_ERROR  (error == NO_MEM_ERROR || error == MEM_LIMIT_ERROR)

#endif /* _EVAL_H_ */

