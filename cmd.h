/* public things from cmd.c */

#ifndef _CMD_H_
#define _CMD_H_

typedef struct cmdstruct {
    char *sortname;             /* set to NULL if you want to sort by
                                 * command name */
    char *name;			/* command name */
    char *help;			/* short help */
    function_str funct;		/* function to call */
    struct cmdstruct *next;
} cmdstruct;

extern cmdstruct *commands;

void show_stat		__P ((void));

void cmd_add_command( cmdstruct *cmd );

void initialize_cmd(void);

int print_all_options __P1 (FILE *,file);

#endif /* _CMD_H_ */

