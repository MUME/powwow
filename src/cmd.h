/* public things from cmd.c */

#ifndef _CMD_H_
#define _CMD_H_

typedef struct cmdstruct {
    const char *sortname;       /* set to NULL if you want to sort by
                                 * command name */
    const char *name;           /* command name */
    const char *help;           /* short help */
    function_str funct;		/* function to call */
    struct cmdstruct *next;
} cmdstruct;

extern cmdstruct *commands;

/* defined by a plugin and called when #module loads it */
void powwow_init(void);

void cmd_add_command( cmdstruct *cmd );

void initialize_cmd(void);

int print_all_options(FILE *file);

/* the name of option 'i', or NULL after the last one */
const char *option_name(int i);
const char *command_option_name(int i);

#endif /* _CMD_H_ */

