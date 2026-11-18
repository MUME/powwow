/* public things from cmd.c */

#ifndef _CMD_H_
#define _CMD_H_

typedef struct cmdstruct {
    const char *sortname;       /* set to NULL if you want to sort by
                                 * command name */
    const char *name;           /* command name */
    const char *help;           /* short help */
    function_str funct;         /* function to call */
    struct cmdstruct *next;
} cmdstruct;

extern cmdstruct *commands;

/* defined by a plugin and called when #module loads it */
void powwow_init(void);

void cmd_add_command( cmdstruct *cmd );

void initialize_cmd(void);

/* the options that print_options() prints */
enum option_scope {
    OPTIONS_ALL,
    OPTIONS_LOCAL,              /* those of a definition file */
    OPTIONS_GLOBAL,             /* those of the user-wide settings file */
};

int print_options(FILE *file, enum option_scope scope);

/* the user-wide settings as when powwow starts */
void reset_user_wide_settings(void);

/* show the attributes of #hilite as they are now */
void hilite_update(void);

/* the name of option 'i', or NULL after the last one */
const char *option_name(int i);
const char *command_option_name(int i);

/* whether #secret is reading a secret, and the 'len' bytes at 's' read,
 * none if cancelled */
bool secret_reading(void);
void secret_entered(const char *s, size_t len);

/* the command for /bin/sh that runs 'arg' as #! does, to be freed */
char *shell_command(const char *arg, const char *file);

#endif /* _CMD_H_ */

