/* public things from cmd.c */

#ifndef _CMD_H_
#define _CMD_H_

typedef struct cmdstruct {
    struct cmdstruct *next;
    char *sortname;             /* set to NULL if you want to sort by command name */
    char *name;			/* command name */
    function_str funct;		/* function to call */
    char *help;			/* short help */
} cmdstruct;

extern cmdstruct *commands;

void show_stat		__P ((void));

void cmd_add_command( cmdstruct *cmd );

void initialize_cmd(void);

int print_all_options __P1 (FILE *,file);

#endif /* _CMD_H_ */

