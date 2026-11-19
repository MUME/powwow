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

/* for --get and --set: print the setting 'name' on standard output, or
 * set one from 'name=value'; return false if there is no such setting, or
 * its value is not valid */
bool print_setting(const char *name);
bool set_setting(const char *spec);
bool setting_is_global(const char *name);
void print_settings(void);

/* the most settings of --get */
#define SETTINGS_MAX 64

/* the names of the settings of --get, sorted, in 'names', which has room
 * for 'size'; return how many there are */
size_t setting_names(const char **names, size_t size);

/* what the setting 'name' of --get takes */
enum setting_kind {
    SETTING_COMMAND,            /* a command, such as editor */
    SETTING_ON_OFF,             /* on or off, such as save */
    SETTING_HOST,               /* host, as #host takes it */
    SETTING_ATTR,               /* an attribute, such as hilite-input */
    SETTING_NUMBER,             /* a number, such as keepalive */
    SETTING_FILE,               /* a file of powwow, which --set cannot set */
};
enum setting_kind setting_kind(const char *name);

/* #setvar keepalive in effect: the MUD's own, or else the user-wide one */
void keepalive_update(void);

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

