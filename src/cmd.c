/*
 *  cmd.c  --  functions for powwow's built-in #commands
 *
 *  (created: Finn Arne Gangstad (Ilie), Dec 25th, 1993)
 *
 *  Copyright (C) 1998 by Massimiliano Ghilardi
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 */

#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <ctype.h>
#include <time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#include "defines.h"

#ifdef HAVE_LIBDL
#include <dlfcn.h>
#endif

#include "main.h"
#include "utils.h"
#include "beam.h"
#include "cmd.h"
#include "cmd2.h"
#include "charname.h"
#include "edit.h"
#include "list.h"
#include "map.h"
#include "tcp.h"
#include "tls.h"
#include "tty.h"
#include "eval.h"
#include "log.h"

/*           local function declarations            */
#define F(name) cmd_ ## name(char *arg)

static void F(help), F(action), F(add),
  F(addstatic), F(alias), F(at), F(beep), F(bind),
  F(cancel), F(capture), F(clear), F(connect), F(cpu),
  F(do), F(delim), F(edit), F(emulate), F(exe),
  F(file), F(for), F(hilite), F(history), F(host),
  F(identify), F(if), F(in), F(init), F(isprompt),
  F(key), F(keyedit),
  F(load), F(map), F(mark), F(movie),
  F(net), F(nice), F(option),
  F(prefix), F(print), F(prompt), F(put),
  F(qui), F(quit), F(quote),
  F(rawsend), F(rawprint), F(rebind), F(rebindall), F(rebindALL),
  F(record), F(request), F(reset), F(retrace),
  F(save), F(send), F(setvar), F(snoop), F(spawn), F(stop),
  F(substitute), F(time), F(var), F(ver), F(while), F(write),
  F(eval), F(zap), F(group), F(speedwalk), F(groupdelim);
#ifdef USE_SHELL
static void F(shell);
#endif
#ifdef HAVE_LIBDL
static void F(module);
#endif

#undef F

/* This must be init'd now at runtime */
cmdstruct *commands = NULL;

#define C(cname, cfunct, chelp) \
    { .name = (cname), .help = (chelp), .funct = (cfunct) }

/* The builtin commands */
static cmdstruct default_commands[] =
{
    C("help",       cmd_help,
      "[-P|--no-pager] [keys|math|<command>]\tthis text, or help on"
      " specified topic"),
    C("17", (function_str)0,
      "<command>\trepeat <command> 17 times"),
#ifdef USE_SHELL
    C("!",          cmd_shell,
      "<shell-command>\texecute a shell command using /bin/sh, or"
      " #option shell"),
#endif
    C("action",     cmd_action,
      "[[<|=|>|%][+|-]<label>] [{<pattern>|(<expr>)}=[<command>]]\t"
      "delete/list/define actions"),
    C("add",        cmd_add,
      "{<text>|(<expr>)}\tadd the string to word completion list"),
    C("addstatic",  cmd_addstatic,
      "{<text>|(<expr>)}\tadd the string to the static completion list"),
    C("alias",      cmd_alias,
      "[<name>[=[<command>]]]\tdelete/list/define aliases"),
    C("at",         cmd_at,
      "[<label> [(<time>) [<command>]]]\tset time of delayed label"),
    C("beep",       cmd_beep,
      "\tmake your terminal beep (like #print (*7))"),
    C("bind",       cmd_bind,
      "[edit|functions|<key> [<sequence>][=[<command>]]]\tdelete/list/define"
      " key bindings"),
    C("cancel",     cmd_cancel,
      "[<number>]\tcancel editing session"),
    C("capture",    cmd_capture,
      "[[>]<filename>]\tbegin/end of capture to file"),
    C("clear",      cmd_clear,
      "\tclear input line (use from spawned programs)"),
    C("connect",    cmd_connect,
      "[<session-id> [<initstr>] [<address> <port>]]\topen a new connection"),
    C("cpu",        cmd_cpu,
      "\tshow CPU time used by powwow"),
    C("delim",      cmd_delim,
      "[normal|program|{custom <chars>}]\tset word completion delimeters"),
    C("do",         cmd_do,
      "(<expr>) <command>\trepeat <command> <expr> times"),
    C("edit",       cmd_edit,
      "\tlist editing sessions"),
    C("emulate",    cmd_emulate,
      "[<|!]{<text>|(<expr>)}\tprocess result as if received from host"),
    C("exe",        cmd_exe,
      "[<|!]{<text>|(<expr>)}\texecute result as if typed from keyboard"),
    C("file",       cmd_file,
      "[=[<filename>]]\tset/show powwow definition file"),
    C("for",        cmd_for,
      "([<init>];<check>;[<loop>]) <command>\t"
      "while <check> is true execute <command>"),
    C("group",      cmd_group,
      "[<name>] [on|off|list]\tgroup alias/action manipulation"),
    C("groupdelim", cmd_groupdelim,
      "[<delimiter>]\tchange delimiter for action/alias groups"),
    C("hilite",     cmd_hilite,
      "[input|messages] [<attribute>]\thighlight your input line or"
      " powwow's messages"),
    C("history",    cmd_history,
      "[{<number>|(<expr>)}]\tlist/execute commands in history"),
    C("host",       cmd_host,
      "[<hostname> <port> [--tls[=<options>]|--no-tls] [--utf8|--latin1]]\t"
      "set/show address of default host"),
    C("identify",   cmd_identify,
      "[<startedit> [<endedit>]]\tsend MUME client identification"),
    C("if",         cmd_if,
      "(<expr>) <command1> [; #else <command2>]\t"
      "if <expr> is true execute <command1>,"
      " otherwise execute <command2>"),
    C("in",         cmd_in,
      "[<label> [(<delay>) [<command>]]]\tdelete/list/define delayed labels"),
    C("init",       cmd_init,
      "[=[<command>]]\tdefine command to execute on connect to host"),
    C("isprompt",   cmd_isprompt,
      "[{<number>|(<expr>)}]\trecognize a prompt as such"),
    C("key",        cmd_key,
      "<name>\texecute the <name> key binding"),
    C("keyedit",    cmd_keyedit,
      "<function>\trun a line-editing function"),
    C("load",       cmd_load,
      "[<filename>]\tload powwow settings from file"),
    C("map",        cmd_map,
      "[-[<number>]|<walk-sequence>]\tshow/clear/edit (auto)map"),
    C("mark",       cmd_mark,
      "[<pattern>[=[<attribute>]]]\tdelete/list/define markers"),
#ifdef HAVE_LIBDL
    C("module",     cmd_module,
      "[<module-name>]\tload shared library extension"),
#endif
    C("movie",      cmd_movie,
      "[<filename>]\tbegin/end of movie record to file"),
    C("net",        cmd_net,
      "\tprint amount of data received from/sent to host"),
    C("nice",       cmd_nice,
      "[{<number>|(<expr>)} [<command>]]\tset/show priority of new"
      " actions/marks"),
    C("option",     cmd_option,
      "[[+|-|=]<option>|list]...\tlist or view various options, or set"
      " <option>=<command>"),
    C("prefix",     cmd_prefix,
      "[<string>]\tprefix all lines with string"),
    C("",           cmd_eval,
      "(<expr>)\tevaluate expression, trashing result"),
    C("print",      cmd_print,
      "[<|!][<text>|(<expr>)]\t"
      "print text/result on screen, appending a \\n"
      "\n"
      "if no argument, prints value of variable $0"),
    C("prompt",     cmd_prompt,
      "[[<|=|>|%][+|-]<label>] [{<pattern>|(<expr>)}=[<command>]]\t"
      "delete/list/define actions on prompts"),
    C("put",        cmd_put,
      "{<text>|(<expr>)}\tput text/result of expression in history"),
    C("qui",        cmd_qui,
      "\tdo nothing"),
    C("quit",       cmd_quit,
      "\tquit powwow"),
    C("quote",      cmd_quote,
      "[on|off]\ttoggle verbatim-flag on/off"),
    C("rawsend",    cmd_rawsend,
      "{<text>|(<expr>)}\tsend raw data to the MUD"),
    C("rawprint",   cmd_rawprint,
      "{<text>|(<expr>)}\tsend raw data to the screen"),
    C("rebind",     cmd_rebind,
      "<key> [<sequence>]\tchange sequence of a key binding"),
    C("rebindall",  cmd_rebindall,
      "\trebind all key bindings"),
    C("rebindALL",  cmd_rebindALL,
      "\trebind ALL key bindings, even trivial ones"),
    C("record",     cmd_record,
      "[<filename>]\tbegin/end of record to file"),
    C("request",    cmd_request,
      "[editor][all]\tsend various identification strings"),
    C("reset",      cmd_reset,
      "{all|<list-name>|bind [<key>]}\tclear the whole defined list"
      " and reload"
      " default, or reset one key"),
    C("retrace",    cmd_retrace,
      "[<number>]\tretrace the last number steps"),
    C("save",       cmd_save,
      "[<filename>]\tsave powwow settings to file"),
    C("send",       cmd_send,
      "[<|!]{<text>|(<expr>)}\teval expression, sending result to the MUD"),
    C("setvar",     cmd_setvar,
      "<name>[={<number>|(<expr>)}]\tset/show internal limits and variables"),
    C("snoop",      cmd_snoop,
      "<session-id>\ttoggle output display for connections"),
    C("spawn",      cmd_spawn,
      "<session-id> <command>\ttalk with a shell command"),
    C("speedwalk",  cmd_speedwalk,
      "[<walk-sequence>]\texecute a speedwalk sequence explicitly"),
    C("stop",       cmd_stop,
      "\tremove all delayed commands from active list"),
    C("substitute", cmd_substitute,
      "[<pattern>[=[<text>]]]\tdelete/list/define substitutions"),
    C("time",       cmd_time,
      "\tprint current time and date"),
    C("var",        cmd_var,
      "<variable>[=[[<|!]{<text>|(<expr>)}]]\twrite result into the variable"),
    C("ver",        cmd_ver,
      "[-v]\tshow the version, or also credits and options"),
    C("while",      cmd_while,
      "(<expr>) <command>\twhile <expr> is true execute <command>"),
    C("write",      cmd_write,
      "[>|!](<expr>;<filename>)\twrite the result of <expr> to <filename>"),
    C("zap",        cmd_zap,
      "<session-id>\tclose a connection"),
    { 0 }
};

static const char *cmd_sort_name(cmdstruct *cmd)
{
    return cmd->sortname == NULL ? cmd->name : cmd->sortname;
}

/* Adds a cmd to commands (inserts the ptr in the list, DO NOT FREE IT) */
void cmd_add_command(cmdstruct *cmd)
{
    /*
     * make sure it doesn't override another commmand
     * this is important not just because it'd be irritating,
     * but if a module defined the command in the global static
     * space, it would create an infinite loop because the -> next
     * ptr would point at itself
     *
     * doing it up front because based on the sortname, we may never see
     * the dup item if we do it at sort time
     */
    for (cmdstruct *c = commands; c != NULL; c = c->next) {
        if (strcmp(cmd->name, c->name) == 0) {
            message_printf("#error %s is already defined\n", c->name);
            return;
        }
    }


    /* catch insertion to head of list */
    if (commands == NULL) {
        commands = cmd;
        cmd->next = NULL;
        return;
    }

    if (strcmp(cmd_sort_name(commands), cmd_sort_name(cmd)) > 0) {
        /* this is lower in sort than every item, so
         * make it the head of the list */
        cmd->next = commands;
        commands = cmd;
        return;
    }

    for (cmdstruct *c = commands; c != NULL; c = c->next) {
        if (strcmp(cmd_sort_name(cmd), cmd_sort_name(c)) >= 0) {
            /* Need second check to handle empty string case */
            if (c->next == NULL
                || strcmp(cmd_sort_name(cmd), cmd_sort_name(c->next)) <= 0) {
                /* insert after this one, it is greater than this
                 * entry but less than the next */
                cmd->next = c->next;
                c->next = cmd;
                return;
            }
        }
    }

    message_printf("ERROR INSERTING COMMAND\n");
}

/* Init the command listing, called from main */
void initialize_cmd(void)
{
    for (int i = 0; default_commands[i].name != NULL; ++i)
        cmd_add_command(&default_commands[i]);
}

#ifdef HAVE_LIBDL
static void cmd_module(char *arg)
{
    arg = skipspace(arg);

    static const char *const prefixes[] = {
        PLUGIN_DIR,
        ".",
        "/lib/powwow",
        "/usr/lib/powwow",
        "/usr/local/lib/powwow",
    };
    char libname[1024];

    /*
     * use the first of the directories that has <arg>.so; if none has it,
     * dlopen() below reports why the one in the last directory cannot be
     * loaded
     */
    for (size_t pindex = 0; pindex < sizeof(prefixes) / sizeof(*prefixes);
         ++pindex) {
        /* only <arg>.so: a file named <arg>, such as in the current
         * directory, is probably not a library */
        struct stat junk;
        snprintf(libname, 1024, "%s/%s.so", prefixes[pindex], arg);
        if (stat(libname, &junk) == 0) {
            break;
        }
    }

    void *lib = dlopen(libname, RTLD_GLOBAL | RTLD_LAZY);
    if (lib == NULL) {
        message_printf("#module error: %s\n", dlerror());
        return;
    } else {
        message_printf("#module loaded %s\n", libname);
    }

    void (*func)(void) = dlsym(lib, "powwow_init");
    if (func != NULL) {
        (*func)();
    } else {
        message_printf("#module error: %s\n", dlerror());
    }
}
#endif

static void cmd_group(char *arg)
{
    arg = skipspace(arg);

    if (*arg != '\0') {
        char *group = arg;
        arg = first_regular(group, ' ');
        *arg = '\0';
        arg = strip_end(skipspace(arg + 1));

        bool active;
        if (strcmp(arg, "on") == 0) {
            active = true;
        } else if (strcmp(arg, "off") == 0) {
            active = false;
        } else if (strcmp(arg, "list") == 0) {
            show_group(group);
            return;
        } else {
            message_printf("#unknown group command, use off/on/list\n");
            return;
        }

        for (aliasnode *p = sortedaliases; p != NULL; p = p->snext) {
            if (p->group != NULL && strcmp(p->group, group) == 0) {
                p->active = active;
            }
        }

        for (actionnode *a = actions; a != NULL; a = a->next) {
            if (a->group != NULL && strcmp(a->group, group) == 0) {
                a->active = active;
            }
        }
    } else {
        message_printf("#group name required\n");
    }
}

static void cmd_groupdelim(char *arg)
{
    if (*arg != 0) {
        free(group_delim);
        group_delim = my_strdup(arg);
    }
}

/*
 * find the help entry for 'arg', leaving its first line in 'line'; with
 * 'exact' false, 'arg' can be an abbreviation of the name of the entry
 */
static bool find_help_entry(FILE *f, char *line, int size, const char *arg,
                            bool exact)
{
    const size_t len = strlen(arg);

    rewind(f);
    while (utf8_fgets(line, size, f) != NULL) {
        if (line[0] == '@' && strncmp(line + 1, arg, len) == 0
            && (!exact || line[len + 1] == '\n' || line[len + 1] == '\0'))
            return true;
    }
    return false;
}

/* column where the descriptions in the list of commands start */
enum { HELP_COLUMN = 32 };

/*
 * write the line(s) for 'c' in the list of commands to 'dst', and
 * return the end of what was written. A tab in the help text separates
 * the arguments from the description, which starts at HELP_COLUMN and is
 * wrapped there to wrap_width(); a newline in it starts a new line.
 */
static char *format_command_help(char *dst, const cmdstruct *c)
{
    const char *tab = strchr(c->help, '\t');

    if (tab == NULL) {
        sprintf(dst, "#%s %s\n", c->name, c->help);
        return dst + strlen(dst);
    }

    /* a command named by punctuation, as #!, needs no space: #!ls */
    int args_len = (int)(tab - c->help);
    unsigned char first = c->name[0];
    bool punct = (first <= ASCII_MAX && ispunct(first) && c->name[1] == '\0');
    bool space = args_len > 0 && !punct;
    sprintf(dst, "#%s%s%.*s", c->name, space ? " " : "", args_len,
            c->help);
    size_t col = strlen(dst);
    dst += col;
    if (col >= HELP_COLUMN) {
        *dst++ = '\n';
        col = 0;
    }
    size_t width = wrap_width();
    for (const char *p = tab + 1; *p != '\0'; ) {
        if (*p == '\n') {
            *dst++ = *p++;
            col = 0;
            continue;
        }
        /* the spaces before a word, then the word */
        size_t spaces = strspn(p, " ");
        const char *word = p + spaces;
        size_t len = strcspn(word, " \n");
        if (col > HELP_COLUMN && col + spaces + len > width) {
            *dst++ = '\n';
            col = 0;
        } else if (col >= HELP_COLUMN) {
            memcpy(dst, p, spaces);
            dst += spaces;
            col += spaces;
        }
        for (; col < HELP_COLUMN; ++col)
            *dst++ = ' ';
        memcpy(dst, word, len);
        dst += len;
        col += len;
        p = word + len;
    }
    *dst++ = '\n';
    *dst = '\0';
    return dst;
}

/*
 * show 'text', whose first line is its title, with the pager, or on
 * the screen if 'use_pager' is false; this frees 'text'
 */
static void show_help_text(char *text, bool use_pager)
{
    if (use_pager) {
        message_edit(NULL, text, strlen(text), true, true);
        return;
    }
    const char *body = strchr(text, '\n');
    message_printf("%s", body != NULL ? body + 1 : text);
    free(text);
}

/*
 * append the file 'path' to the 'len' bytes of '*text', after an empty
 * line if 'sep', ending it with '\0'; return false, and leave '*text' as
 * it was, if it cannot be read
 */
static bool append_file(char **text, size_t *len, const char *path,
                        bool sep)
{
    FILE *f = fopen(path, "r");
    struct stat st;
    if (f == NULL || fstat(fileno(f), &st) < 0) {
        if (f != NULL)
            fclose(f);
        return false;
    }
    size_t gap = sep ? 1 : 0;
    char *new_text = xrealloc(*text, *len + gap + st.st_size + 1);
    *text = new_text;
    if (sep)
        new_text[*len] = '\n';
    size_t got = fread(new_text + *len + gap, 1, st.st_size, f);
    bool ok = ferror(f) == 0;
    fclose(f);
    if (ok)
        *len += gap + got;
    new_text[*len] = '\0';
    return ok;
}

/*
 * the file 'name' in the directory of the file 'path', in 'buf' of
 * BUFSIZE bytes
 */
static const char *file_beside(char *buf, const char *path,
                               const char *name)
{
    const char *slash = strrchr(path, '/');
    int dir_len = slash != NULL ? (int)(slash - path) + 1 : 0;
    snprintf(buf, BUFSIZE, "%.*s%s", dir_len, path, name);
    return buf;
}

/*
 * show the license of powwow, with a title as the help texts have, and
 * then the licenses of what comes with it: those that LICENSES.txt
 * beside COPYING lists, if there is one, and that of the Unicode data of
 * the names
 */
static void show_copyright(bool use_pager)
{
    static const char title[] = "Help on 'copyright'\n";
    size_t len = sizeof title - 1;
    char *text = xstrdup(title);
    if (!append_file(&text, &len, copyfile, false)) {
        wrap_printf("#cannot read \"%s\": %s", copyfile, strerror(errno));
        free(text);
        return;
    }
    char path[BUFSIZE];
    append_file(&text, &len, file_beside(path, copyfile, "LICENSES.txt"),
                true);
    append_file(&text, &len,
                file_beside(path, namesfile, "unicode-license.txt"), true);
    show_help_text(text, use_pager);
}

static void cmd_help(char *arg)
{
    arg = skipspace(arg);
    bool use_pager = true;
    size_t flag_len = strcspn(arg, " \t");
    if ((flag_len == 2 && strncmp(arg, "-P", 2) == 0)
        || (flag_len == 10 && strncmp(arg, "--no-pager", 10) == 0)) {
        use_pager = false;
        arg = skipspace(arg + flag_len);
    }
    strip_end(arg);
    if (*arg == '#') ++arg;
    if (*arg == '\0') {
        int size = 25;
        for (cmdstruct *c = commands; c != NULL; c = c->next) {
            size += strlen(c->name) + strlen(c->help) + 5;
            /* a line can start at each space or newline */
            for (const char *p = c->help; *p != '\0'; ++p) {
                if (*p == ' ' || *p == '\n')
                    size += HELP_COLUMN + 1;
            }
            size += HELP_COLUMN + 1;
        }

        char *text = xmalloc(size);
        char *tmp = text;

        /* do not use the return value of sprintf(), almost every OS returns
         * a different thing */
        sprintf(tmp, "#help\n#commands available:\n");
        tmp += strlen(tmp);

        for (cmdstruct *c = commands; c != NULL; c = c->next)
            tmp = format_command_help(tmp, c);

        show_help_text(text, use_pager);
        return;
    }

    if (strncmp(arg, "copyright", strlen(arg)) == 0) {
        show_copyright(use_pager);
        return;
    }

    /* !copyright */

    FILE *f = fopen(helpfile, "r");
    if (f == NULL) {
        wrap_printf("#cannot open help file \"%s\": %s", helpfile,
                    strerror(errno));
        return;
    }

    char line[BUFSIZE];
    /* an exact match comes first: otherwise "in" would find "info" */
    if (!find_help_entry(f, line, BUFSIZE, arg, true)
        && !find_help_entry(f, line, BUFSIZE, arg, false)) {
        wrap_printf("#no entry for \"%s\" in the help file.", arg);
        fclose(f);
        return;
    }

    int size = BUFSIZE + 32;
    char *text = xmalloc(size);

    /* the first line becomes $TITLE */
    char *tmp = strchr(line, '\n');
    if (tmp != NULL) *tmp = '\0';
    int i = sprintf(text, "Help on '%s'\n", line + 1);

    /* allow multiple commands to share the same help */
    while (utf8_fgets(line, BUFSIZE, f) != NULL && line[0] == '@') ;

    do {
        int len = strlen(line);
        if (len >= size - i) {
            /* Not enough space in current buffer */

            size += BUFSIZE;
            text = xrealloc(text, size);
        }
        memcpy(text + i, line, len);
        i += len;
    } while (utf8_fgets(line, BUFSIZE, f) != NULL && line[0] != '@');

    fclose(f);
    text[i] = '\0'; /* safe, there is space */
    show_help_text(text, use_pager);
}

static void cmd_clear(char *arg)
{
    if (line_status == 0) {
        clear_input_line(opt_compact);
        if (!opt_compact) {
            tty_putc('\n');
            col0 = 0;
        }
        status(1);
    }
}

/*
 * the command for /bin/sh that #!, and the ! of #var and others, run
 * 'arg' with: 'arg' itself, or, if POWWOWSHELL is set, such as to
 * "zsh -c", that shell with 'arg' as its last argument, to be freed
 */
static char *shell_command(const char *arg)
{
    const char *shell = opt_shell != NULL ? opt_shell : getenv("POWWOWSHELL");
    if (shell == NULL || *shell == '\0')
        return xstrdup(arg);

    /* exec $POWWOWSHELL 'arg', with each ' of 'arg' as '\'' */
    static const char quote[] = "'\\''";
    size_t len = strlen("exec  ''") + strlen(shell);
    for (const char *p = arg; *p != '\0'; ++p)
        len += *p == '\'' ? sizeof quote - 1 : 1;
    char *command = xmalloc(len + 1);
    char *q = command + sprintf(command, "exec %s '", shell);
    for (const char *p = arg; *p != '\0'; ++p) {
        if (*p == '\'') {
            memcpy(q, quote, sizeof quote - 1);
            q += sizeof quote - 1;
        } else {
            *q++ = *p;
        }
    }
    strcpy(q, "'");
    return command;
}

/*
 * popen() the command 'command', as #! runs it: with #option shell or
 * POWWOWSHELL if set; NULL on errors
 */
static FILE *shell_popen(const char *command, const char *mode)
{
    char *line = shell_command(command);
    FILE *fp = popen(line, mode);
    free(line);
    return fp;
}

#ifdef USE_SHELL
static void cmd_shell(char *arg)
{
    if (*arg == '\0') {
        if (opt_info) {
            message_printf("#that's easy.\n");
        }
    } else {
        char *command = shell_command(arg);
        tty_quit();
        int wstatus = system(command);
        free(command);
        tty_start();
        col0 = 0;
        line0 = lines - 1;
        tty_gotoxy(col0, line0);
        tty_puts(tty_clreoln);

        if (wstatus == -1) {
            errmsg("cannot run the shell");
        } else if (WIFSIGNALED(wstatus) && WTERMSIG(wstatus) == SIGINT) {
            /* stop a loop, as shells do */
            message_printf("#interrupted.\n");
            error = USER_BREAK;
        }
    }
}
#endif

static void cmd_alias(char *arg)
{
    arg = skipspace(arg);
    if (*arg == '\0')
        show_aliases();
    else
        parse_alias(arg);
}

static void cmd_action(char *arg)
{
    arg = skipspace(arg);
    if (*arg == '\0')
        show_actions();
    else
        parse_action(arg, false);
}

static void cmd_prompt(char *arg)
{
    arg = skipspace(arg);
    if (*arg == '\0')
        show_prompts();
    else
        parse_action(arg, true);
}

static void cmd_beep(char *arg)
{
    tty_putc(ASCII_BEL);
}

/*
 * create/list/edit/delete bindings
 */
static void cmd_bind(char *arg)
{
    arg = skipspace(arg);
    if (strchr(arg, '=') == NULL)
        strip_end(arg);         /* not in the command bound */
    if (*arg == '\0')
        show_binds(false);
    else if (strcmp(arg, "edit") == 0)
        show_binds(true);
    else if (strcmp(arg, "functions") == 0)
        show_edit_functions();
    else
        parse_bind(arg);
}

static void cmd_delim(char *arg)
{
    arg = skipspace(arg);
    if (*arg == '\0') {
        wrap_printf("#delim: \"%s\" (%s)", delim_name[delim_mode], DELIM);
        return;
    }

    char buf[BUFSIZE];
    arg = split_first_word(buf, BUFSIZE, arg);
    int n = 0;
    while (n < DELIM_MODES && strncmp(delim_name[n], buf, strlen(buf)) != 0)
        ++n;

    if (n >= DELIM_MODES) {
        message_printf("#delim [normal|program|{custom <chars>}\n");
        return;
    }

    if (n == DELIM_CUSTOM) {
        if (strchr(arg, ' ') == NULL) {
            my_strncpy(buf + 1, arg, BUFSIZE - 2);
            *buf = ' ';                         /* force ' ' in the delims */
            arg = buf;
        }
        unescape(arg);
        set_custom_delimeters(arg);
    } else
        delim_mode = n;
}

static void cmd_do(char *arg)
{
    arg = skipspace(arg);
    if (*arg != '(') {
        message_printf("#do: ");
        error = MISMATCH_PAREN_ERROR;
        print_error(error);
        return;
    }
    ++arg;

    long result;
    int type = evall(&result, &arg);
    if (REAL_ERROR) return;

    if (type != TYPE_NUM) {
        message_printf("#do: ");
        error = NO_NUM_VALUE_ERROR;
        print_error(error);
        return;
    }

    if (*arg == ')') {          /* skip the ')' */
        if (*++arg == ' ')
            ++arg;
    } else {
        message_printf("#do: ");
        error = MISSING_PAREN_ERROR;
        print_error(error);
        return;
    }

    if (result >= 0)
        while (error == 0 && result-- != 0)
            parse_instruction(arg, true, false, true);
    else {
        message_printf("#do: bogus repeat count \"%ld\"\n", result);
    }
}

static void cmd_hilite(char *arg)
{
    arg = skipspace(arg);
    size_t len = strcspn(arg, " ");
    bool messages = false;
    if (len == 8 && strncmp(arg, "messages", 8) == 0) {
        messages = true;
        arg = skipspace(arg + len);
    } else if (len == 5 && strncmp(arg, "input", 5) == 0) {
        arg = skipspace(arg + len);
    }

    struct attr attr;
    char why[BUFSIZE];
    if (!attr_parse_why(arg, &attr, why, sizeof(why))) {
        message_printf("#hilite: %s.\n", why);
        if (opt_info) {
            message_printf("#syntax: #hilite [input|messages]"
                           " [<attribute>]\n");
            show_attr_syntax();
        }
    } else if (messages) {
        message_attr = attr;
        attr_string(&attr, msgattrbeg, msgattrend);
        if (opt_info) {
            message_printf("#message highlighting is now %s.\n",
                           attr_is_none(&attr) ? "off" : "on");
        }
    } else {
        input_attr = attr;
        attr_string(&attr, edattrbeg, edattrend);

        edattrbg = ((attr.flags & (ATTR_INVERSE | ATTR_BLINK)) != 0
                    || attr.bg.kind != COLOR_NONE);

        if (opt_info) {
            /* "on" in the input attribute, and then the message one again */
            wrap_printf("#input highlighting is now %s%so%s%s%s.", msgattrend,
                        edattrbeg, attr_is_none(&attr) ? "ff" : "n", edattrend,
                        msgattrbeg);
        }
    }
}

static void cmd_history(char *arg)
{
    arg = skipspace(arg);

    if (history_done >= MAX_HIST) {
        error = HISTORY_RECURSION_ERROR;
        print_error(error);
        return;
    }
    ++history_done;

    int num = 0;
    if (*arg == '(') {
        ++arg;
        long buf;
        num = evall(&buf, &arg);
        if (!REAL_ERROR && num != TYPE_NUM)
            error = NO_NUM_VALUE_ERROR;
        if (REAL_ERROR) {
            message_printf("#history: ");
            error = NO_NUM_VALUE_ERROR;
            print_error(error);
            return;
        }
        num = (int)buf;
    } else
        num = atoi(arg);

    if (num > 0)
        exe_history(num);
    else
        show_history(-num);
}

/*
 * if 'arg' is --utf8 or --latin1, store the charset it selects in *utf8
 * and return true
 */
static bool parse_charset_flag(const char *arg, bool *utf8)
{
    if (strcmp(arg, UTF8_FLAG) == 0)
        *utf8 = true;
    else if (strcmp(arg, LATIN1_FLAG) == 0)
        *utf8 = false;
    else
        return false;
    return true;
}

static void cmd_host(char *arg)
{
    arg = skipspace(arg);
    if (*arg != '\0') {
        char hostname[BUFSIZE];
        arg = split_first_word(hostname, BUFSIZE, arg);
        if (*arg != '\0') {
            char buf[BUFSIZE];
            arg = split_first_word(buf, BUFSIZE, arg);
            int port = atoi(buf);
            struct tls_options tlsopt = { .use_tls = default_tls };
            bool utf8 = default_utf8;
            while (*(arg = skipspace(arg)) != '\0') {
                arg = split_first_word(buf, BUFSIZE, arg);
                if (parse_charset_flag(buf, &utf8))
                    continue;
                if (strcmp(buf, NO_TLS_FLAG) == 0) {
                    tlsopt = (struct tls_options){ .use_tls = false };
                    continue;
                }
                struct tls_options t = { .use_tls = false };
                const char *targ = buf;
                if (!parse_tls_flag(&t, &targ, true) || !t.use_tls)
                    goto fail;
                tlsopt = t;
            }

            my_strncpy(hostinfo.name, hostname, BUFSIZE - 1);
            hostinfo.port = port;
            hostinfo.tlsopt = tlsopt;
            hostinfo.utf8 = utf8;

            if (opt_info)
                message_printf("#host set to: %s %d%s%s\n", hostinfo.name,
                               hostinfo.port, tlsopt_str(&hostinfo.tlsopt),
                               charset_flag_str(utf8));
            return;
        }
    } else if (*hostinfo.name != '\0') {
        fmt_next_edit("#host %s %d%s%s",
                      hostinfo.name, hostinfo.port,
                      tlsopt_str(&hostinfo.tlsopt),
                      charset_flag_str(hostinfo.utf8));
        return;
    }

 fail:
    error = NOT_SUPPORTED_ERROR;
    message_printf("#syntax: #host <hostname> <port> [--tls[=<options>]|"
                   NO_TLS_FLAG "] [" UTF8_FLAG "|" LATIN1_FLAG "]\n");
}

static void cmd_request(char *arg)
{
    if (current_connection == NULL) {
        message_printf("#not connected to a MUD!\n");
        return;
    }
    while (*(arg = skipspace(arg)) != '\0') {
        char buf[256];
        arg = split_first_word(buf, 256, arg);
        if (*buf != '\0') {
            bool all = (strcmp(buf, "all") == 0);
            int len = strlen(buf);
            if (all || strncmp(buf, "editor", len) == 0) {
                mpi_identify(current_connection);
                if (opt_info) {
                    message_printf("#request editor\n");
                }
            }
        }
    }

}

static void cmd_identify(char *arg)
{
    edit_start[0] = '\0';
    edit_end[0] = '\0';
    if (*arg != '\0') {
        char *p = strchr(arg, ' ');
        if (p != NULL) {
            *(p++) = '\0';
            my_strncpy(edit_end, p, BUFSIZE - 1);
        }
        my_strncpy(edit_start, arg, BUFSIZE - 1);
    }
    char request[] = "editor";
    cmd_request(request);
}

static void cmd_in(char *arg)
{
    arg = skipspace(arg);
    if (*arg == '\0') {
        show_delays();
        return;
    }

    char *name = arg;
    arg = first_regular(name, ' ');
    if (*arg != '\0')
        *arg++ = 0;

    unescape(name);

    delaynode **p = lookup_delay(name, false);
    if (*p == NULL)  p = lookup_delay(name, true);

    if (*arg == '\0' && *p == NULL) {
        wrap_printf("#unknown delay label, cannot show: \"%s\"", name);
        return;
    }
    if (*arg == '\0') {
        show_delaynode(*p, 1);
        return;
    }
    if (*arg != '(') {
        message_printf("#in: ");
        error = MISMATCH_PAREN_ERROR;
        print_error(error);
        return;
    }
    ++arg;    /* skip the '(' */

    long buf;
    int type = evall(&buf, &arg);
    if (!REAL_ERROR) {
        if (type != TYPE_NUM)
            error = NO_NUM_VALUE_ERROR;
        else if (*arg != ')')
            error = MISSING_PAREN_ERROR;
    }
    if (REAL_ERROR) {
        message_printf("#in: ");
        print_error(error);
        return;
    }

    arg = skipspace(arg + 1);
    long millisec = buf;
    if (*p != NULL && millisec != 0)
        change_delaynode(p, arg, millisec);
    else if (*p == NULL && millisec != 0) {
        if (*arg != '\0')
            new_delaynode(name, arg, millisec);
        else {
            wrap_printf("#cannot create delay label without a command.");
        }
    } else if (*p != NULL && millisec == 0) {
        if (opt_info) {
            message_printf("#deleting delay label: %s %s\n", name,
                           (*p)->command);
        }
        delete_delaynode(p);
    } else {
        wrap_printf("#unknown delay label, cannot delete: \"%s\"", name);
    }
}

static void cmd_at(char *arg)
{
    arg = skipspace(arg);
    if (*arg == '\0') {
        show_delays();
        return;
    }

    char *name = arg;
    arg = first_regular(name, ' ');
    if (*arg != '\0')
        *arg++ = 0;

    unescape(name);

    delaynode **p = lookup_delay(name, false);
    if (*p == NULL)  p = lookup_delay(name, true);

    if (*arg == '\0' && *p == NULL) {
        wrap_printf("#unknown delay label, cannot show: \"%s\"", name);
        return;
    }
    if (*arg == '\0') {
        show_delaynode(*p, 2);
        return;
    }
    if (*arg != '(') {
        message_printf("#in: ");
        error = MISMATCH_PAREN_ERROR;
        print_error(error);
        return;
    }
    ++arg;    /* skip the '(' */

    ptr *pbuf = NULL;
    evalp(&pbuf, &arg);
    if (REAL_ERROR) {
        print_error(error);
        ptrdel(&pbuf);
        return;
    }
    char *buf = NULL;
    int hour = -1, minute = -1, second = -1;
    if (pbuf != NULL) {
        /* convert time-string into hour, minute, second */
        buf = skipspace(ptrdata(pbuf));
        if (*buf == '\0' || !isdigit((unsigned char)*buf)) {
            message_printf("#at: ");
            error = NO_NUM_VALUE_ERROR;
            print_error(error);
            ptrdel(&pbuf);
            return;
        }
        int num = atoi(buf);
        second = num % 100;
        num /= 100;
        minute = num % 100;
        hour = num / 100;
    }
    if (hour < 0 || hour > 23 || minute < 0 || minute > 59
        || second < 0 || second > 59) {

        wrap_printf("#at: #error: invalid time \"%s\"",
                    pbuf != NULL && buf != NULL ? buf : "");
        error = OUT_RANGE_ERROR;
        ptrdel(&pbuf);
        return;
    }
    ptrdel(&pbuf);

    if (*arg == ')') {        /* skip the ')' */
        if (*++arg == ' ')
            ++arg;
    } else {
        message_printf("#at: ");
        error = MISSING_PAREN_ERROR;
        print_error(error);
        return;
    }

    arg = skipspace(arg);
    update_now();
    struct tm *twhen = localtime(&now.tv_sec);
    /* put current year, month, day in calendar struct */

    /* it is NOT possible to define an #at refering to the past: if you
     * use a time smaller than the current, it refers to tomorrow */
    bool tomorrow = (hour < twhen->tm_hour
                     || (hour == twhen->tm_hour
                         && (minute < twhen->tm_min
                             || (minute == twhen->tm_min
                                 && second <= twhen->tm_sec))));

    long millisec = ((hour - twhen->tm_hour) * 3600
                     + (minute - twhen->tm_min) * 60
                     + second - twhen->tm_sec + (tomorrow ? 24 * 60 * 60 : 0));
    millisec *= mSEC_PER_SEC; /* Comparing time with current calendar,
                               we finally got the delay */
    millisec -= now.tv_usec / uSEC_PER_mSEC;

    if (*p != NULL)
        change_delaynode(p, arg, millisec);
    else if (*arg != '\0')
        new_delaynode(name, arg, millisec);
    else {
        wrap_printf("#cannot create delay label without a command.");
    }
}

static void cmd_init(char *arg)
{
    arg = skipspace(arg);

    if (*arg == '=') {
        if (*++arg != '\0') {
            my_strncpy(initstr, arg, BUFSIZE - 1);
            if (opt_info) {
                message_printf("#init: %s\n", initstr);
            }
        } else {
            *initstr = '\0';
            if (opt_info) {
                message_printf("#init cleared.\n");
            }
        }
    } else
        fmt_next_edit("#init =%s", initstr);
}

static void cmd_isprompt(char *arg)
{
    if (current_connection == main_connection) {
        arg = skipspace(arg);
        int i;
        if (*arg == '(') {
            ++arg;
            long l;
            i = evall(&l, &arg);
            if (!REAL_ERROR) {
                if (i != TYPE_NUM)
                  error = NO_NUM_VALUE_ERROR;
                else if (*arg != ')')
                  error = MISSING_PAREN_ERROR;
            }
            if (REAL_ERROR) {
                message_printf("#isprompt: ");
                print_error(error);
                return;
            }
            i = (int)l;
        } else
            i = atoi(arg);

        if (i == 0)
            isprompt_len = -1;
        else if (i < 0) {
            if (i > -PARAM_COUNT && *text_var(-i) != NULL) {
                isprompt_len = ptrlen(*text_var(-i));
                ptrtrunc(prompt->str, isprompt_len);
            }
        } else {
            isprompt_len = i;
            ptrtrunc(prompt->str, isprompt_len);
        }
    }
}

static void cmd_key(char *arg)
{
    arg = strip_end(skipspace(arg));
    if (*arg == '\0')
        return;

    keynode *q = *lookup_key(arg);
    if (q != NULL)
        q->funct(q->call_data);
    else {
        wrap_printf("#no such key: \"%s\"", arg);
    }
}

static void cmd_keyedit(char *arg)
{
    arg = skipspace(arg);
    if (*arg == '\0')
        return;

    char *param;
    int function = lookup_edit_name(arg, &param);
    if (function != 0)
        internal_functions[function].funct(param);
    else {
        wrap_printf("#no such editing function: \"%s\"", arg);
    }
}

static void cmd_map(char *arg)
{
    arg = skipspace(arg);
    if (*arg == '\0')  /* show map */
        map_show();
    else if (*arg == '-')  /* retrace steps without walking */
        map_retrace(atoi(arg + 1), false);
    else
        map_walk(arg, true, true);
}

static void cmd_retrace(char *arg)
{
    map_retrace(atoi(arg), true);
}

static void cmd_mark(char *arg)
{
    if (*arg == '\0')
        show_marks();
    else
        parse_mark(arg);
}

static void cmd_nice(char *arg)
{
    int nnice = a_nice;
    arg = skipspace(arg);
    if (*arg == '\0') {
        message_printf("#nice: %d\n", a_nice);
        return;
    }
    if (isdigit((unsigned char)*arg)) {
        a_nice = 0;
        while (isdigit((unsigned char)*arg)) {
            a_nice *= 10;
            a_nice += *arg++ - '0';
        }
    } else if (*arg++ == '(') {
        long buf;
        int type = evall(&buf, &arg);
        if (!REAL_ERROR && type != TYPE_NUM)
            error = NO_NUM_VALUE_ERROR;
        else if (!REAL_ERROR && *arg++ != ')')
            error = MISSING_PAREN_ERROR;
        if (REAL_ERROR) {
            message_printf("#nice: ");
            print_error(error);
            return;
        }
        a_nice = (int)buf;
        if (a_nice < 0)
            a_nice = 0;
    }
    arg = skipspace(arg);
    if (*arg != '\0') {
        parse_instruction(arg, false, false, true);
        a_nice = nnice;
    }
}

static void cmd_prefix(char *arg)
{
    strcpy(prefixstr, arg);
    if (opt_info) {
        message_printf("#prefix %s.\n", *arg != '\0' ? "set" : "cleared");
    }
}

static void cmd_quote(char *arg)
{
    arg = strip_end(skipspace(arg));
    if (*arg == '\0')
        verbatim = !verbatim;
    else if (strcmp(arg, "on") == 0)
        verbatim = true;
    else if (strcmp(arg, "off") == 0)
        verbatim = false;
    if (opt_info) {
        message_printf("#%s mode.\n", verbatim ? "verbatim" : "normal");
    }
}

/*
 * change the escape sequence of an existing binding
 */
static void cmd_rebind(char *arg)
{
    parse_rebind(arg);
}

static void cmd_rebindall(char *arg)
{
    for (keynode *kp = keydefs; kp != NULL; kp = kp->next) {
        char *seq = kp->sequence;
        if (kp->seqlen == 1 && seq[0] < ' ')
            ;
        else if (kp->seqlen == 2 && seq[0] == ASCII_ESC
                 && isalnum((unsigned char)seq[1]))
            ;
        else {
            parse_rebind(kp->name);
            if (error != 0)
                break;
        }
    }
}

static void cmd_rebindALL(char *arg)
{
    for (keynode *kp = keydefs; kp != NULL; kp = kp->next) {
        parse_rebind(kp->name);
        if (error != 0)
            break;
    }
}

static void reset_usage(const char *arg)
{
    if (*arg == '\0')
        message_printf("#reset: must specify one of:\n");
    else
        wrap_printf("#reset: unknown list \"%s\", must specify one of:", arg);
    tty_puts(" action, alias, all, at, bind [<key>], in, mark, prompt,"
             " substitute, var.\n");
}

static void cmd_reset(char *arg)
{
    arg = skipspace(arg);
    for (size_t len = strlen(arg); len > 0 && arg[len - 1] == ' '; --len)
        arg[len - 1] = '\0';
    if (*arg == '\0') {
        reset_usage(arg);
        return;
    }
    if (strncmp(arg, "bind ", 5) == 0) {
        char name[BUFSIZE];
        split_first_word(name, BUFSIZE, arg + 5);
        reset_bind(name);
        return;
    }
    bool all = (strcmp(arg, "all") == 0);
    if (all || strcmp(arg, "alias") == 0) {
        for (int n = 0; n < MAX_HASH; ++n) {
            while (aliases[n] != NULL)
                delete_aliasnode(&aliases[n]);
        }
        if (!all)
            return;
    }
    if (all || strcmp(arg, "action") == 0) {
        while (actions != NULL)
            delete_actionnode(&actions);
        if (!all)
            return;
    }
    if (all || strcmp(arg, "bind") == 0) {
        while (keydefs != NULL)
            delete_keynode(&keydefs);
        tty_add_initial_binds();
        tty_add_walk_binds();
        if (!all)
            return;
    }
    if (all || strcmp(arg, "in") == 0 || strcmp(arg, "at") == 0) {
        while (delays != NULL)
            delete_delaynode(&delays);
        while (dead_delays != NULL)
            delete_delaynode(&dead_delays);
        if (!all)
            return;
    }
    if (all || strcmp(arg, "mark") == 0) {
        while (markers != NULL)
            delete_marknode(&markers);
        if (!all)
            return;
    }
    if (all || strcmp(arg, "prompt") == 0) {
        while (prompts != NULL)
            delete_promptnode(&prompts);
        if (!all)
            return;
    }
    if (all || strcmp(arg, "substitute") == 0) {
        while (substitutions != NULL)
          delete_substnode(&substitutions);
        if (!all)
          return;
    }
    if (all || strcmp(arg, "var") == 0) {
        for (int n = 0; n < MAX_HASH; ++n) {
            while (named_vars[VAR_NUM].buckets[n] != NULL)
                delete_varnode(&named_vars[VAR_NUM].buckets[n], VAR_NUM);
            varnode **first = &named_vars[VAR_TEXT].buckets[n];
            while (*first != NULL) {
                if (is_permanent_variable(*first))
                    first = &(*first)->next;
                else
                    delete_varnode(first, VAR_TEXT);
            }
        }

        for (int n = -GLOBAL_VAR_COUNT; n < 0; ++n) {
            *num_var(n) = 0;
            ptrdel(text_var(n));
        }
        if (!all)
            return;
    }
    if (!all)
        reset_usage(arg);
}

static void cmd_snoop(char *arg)
{
    if (*arg == '\0') {
        message_printf("#snoop: which connection?\n");
    } else
        tcp_togglesnoop(arg);
}

static void cmd_stop(char *arg)
{
    if (delays != NULL)
        update_now();

    while (delays != NULL) {
        delaynode *dying = delays;
        delays = dying->next;
        dying->when.tv_sec = now.tv_sec;
        dying->when.tv_usec = now.tv_usec;
        dying->next = dead_delays;
        dead_delays = dying;
    }
    if (opt_info) {
        message_printf("#all delayed labels are now disabled.\n");
    }
}

static void cmd_substitute(char *arg)
{
    if (*arg == '\0')
      show_substitutions();
    else
      parse_substitute(arg);
}

static void cmd_time(char *arg)
{
    update_now();
    struct tm *s = localtime(&now.tv_sec);
    char buf[BUFSIZE];
    strftime(buf, BUFSIZE - 1, "%a,  %d %b %Y  %H:%M:%S", s);
    message_printf("#current time is %s\n", buf);
}

static void cmd_ver(char *arg)
{
    arg = strip_end(skipspace(arg));
    if (*arg == '\0')
        printver(0);
    else if (strcmp(arg, "-v") == 0)
        printver(2);
    else
        message_printf("#syntax: #ver [-v]\n");
}

static void cmd_emulate(char *arg)
{
    char kind;
    long start, end;
    ptr *pbuf = NULL;

    arg = redirect(arg, &pbuf, &kind, "emulate", false, &start, &end);
    if (REAL_ERROR || arg == NULL)
        return;

    if (kind != 0) {
        char buf[BUFSIZE];

        FILE *fp = (kind == '!') ? shell_popen(arg, "r") : fopen(arg, "r");
        if (fp == NULL) {
            wrap_printf("#emulate: #error opening \"%s\"", arg);
            error = SYNTAX_ERROR;
            print_error(error);
            ptrdel(&pbuf);
            return;
        }
        status(-1); /* we're pretending we got something from the MUD */
        long i = 1;
        while (error == 0 && (start == 0 || i <= end)
               && utf8_fgets(buf, BUFSIZE, fp) != NULL)
            if (start == 0 || i++ >= start)
                process_remote_input(buf, strlen(buf));

        if (kind == '!') pclose(fp); else fclose(fp);
    } else {
        status(-1); /* idem */
        /* WARNING: no guarantee there is space! */
        int len = strlen(arg);
        arg[len] = '\n';
        arg[++len] = '\0';
        process_remote_input(arg, len);
    }
    ptrdel(&pbuf);
}

static void cmd_eval(char *arg)
{
    arg = skipspace(arg);
    if (*arg == '(') {
        ++arg;
        evaln(&arg);
        if (*arg != ')') {
            message_printf("#(): ");
            error = MISSING_PAREN_ERROR;
            print_error(error);
        }
    } else {
        message_printf("#(): ");
        error = MISMATCH_PAREN_ERROR;
        print_error(error);
    }
}

static void cmd_exe(char *arg)
{
    char kind;
    long start, end;
    ptr *pbuf = NULL;

    arg = redirect(arg, &pbuf, &kind, "exe", false, &start, &end);
    if (REAL_ERROR || arg == NULL)
        return;

    if (kind != 0) {
        char buf[BUFSIZE];

        FILE *fp = (kind == '!') ? shell_popen(arg, "r") : fopen(arg, "r");
        if (fp == NULL) {
            wrap_printf("#exe: #error opening \"%s\"", arg);
            error = SYNTAX_ERROR;
            ptrdel(&pbuf);
            return;
        }
        long offset = 0;
        long i = 1;
        /* We may go in to a loop if a single function is more than 4k, but
         * if that's the case then maybe you should break it down a little
         * bit :p */
        while (error == 0 && (start == 0 || i <= end)
               && utf8_fgets(buf + offset, BUFSIZE - offset, fp) != NULL)
            /* If it ends with \\\n then it's a line continuation, so clear
             * the \\\n and do another fgets */
            if (buf[offset + strlen(buf + offset) - 2] == '\\') {
                /* Clear \n prefixed with a literal backslash '\\' */
                char *clear = strstr(buf + offset, "\\\n");
                if (clear != NULL)
                    *clear = '\0';
                offset += strlen(buf + offset);
            } else {
                if (start == 0 || i++ >= start) {
                    buf[strlen(buf) - 1] = '\0';
                    parse_user_input(buf, false);
                    offset = 0;
                }
            }

        if (kind == '!') pclose(fp); else fclose(fp);
    } else
        parse_user_input(arg, false);
    ptrdel(&pbuf);
}

static void cmd_print(char *arg)
{
    clear_input_line(opt_compact);

    if (*arg == '\0') {
        smart_print(*text_var(0) != NULL ? ptrdata(*text_var(0)) : "", true);
        return;
    }

    char kind;
    long start, end;
    ptr *pbuf = NULL;
    arg = redirect(arg, &pbuf, &kind, "print", true, &start, &end);
    if (REAL_ERROR || arg == NULL)
        return;

    if (kind != 0) {
        char buf[BUFSIZE];
        FILE *fp = (kind == '!') ? shell_popen(arg, "r") : fopen(arg, "r");
        if (fp == NULL) {
            wrap_printf("#print: #error opening \"%s\"", arg);
            error = SYNTAX_ERROR;
            ptrdel(&pbuf);
            return;
        }
        long i = 1;
        while (error == 0 && (start == 0 || i <= end)
               && utf8_fgets(buf, BUFSIZE, fp) != NULL)
            if (start == 0 || i++ >= start)
                tty_puts(buf);
        tty_putc('\n');

        if (kind == '!') pclose(fp); else fclose(fp);
    } else
        smart_print(arg, true);
    ptrdel(&pbuf);
}

static void cmd_send(char *arg)
{
    char kind;
    long start, end;
    ptr *pbuf = NULL;

    arg = redirect(arg, &pbuf, &kind, "send", false, &start, &end);
    if (REAL_ERROR ||arg == NULL)
        return;

    if (kind != 0) {
        char buf[BUFSIZE];
        FILE *fp = (kind == '!') ? shell_popen(arg, "r") : fopen(arg, "r");
        if (fp == NULL) {
            wrap_printf("#send: #error opening \"%s\"", arg);
            error = SYNTAX_ERROR;
            ptrdel(&pbuf);
            return;
        }
        long i = 1;
        while (error == 0 && (start == 0 || i <= end)
               && utf8_fgets(buf, BUFSIZE, fp) != NULL) {
            char *newline = strchr(buf, '\n');
            if (newline != NULL)
                *newline = '\0';

            if (start == 0 || i++ >= start) {
                if (opt_echo) {
                    message_printf("[%s]\n", buf);
                }
                tcp_write(current_connection, buf);
            }
        }
        if (kind == '!') pclose(fp); else fclose(fp);
    } else {
        if (opt_echo) {
            message_printf("[%s]\n", arg);
        }
        tcp_write(current_connection, arg);
    }
    ptrdel(&pbuf);
}

static void cmd_rawsend(char *arg)
{
    char *tmp = skipspace(arg);

    if (*tmp == '(') {
        ptr *pbuf = NULL;
        arg = tmp + 1;
        raw_escapes = true;
        evalp(&pbuf, &arg);
        raw_escapes = false;
        if (REAL_ERROR) {
            print_error(error);
            ptrdel(&pbuf);
        } else if (pbuf != NULL)
            tcp_raw_write(current_connection, ptrdata(pbuf), ptrlen(pbuf));
    } else {
        int len = memunescape(arg, strlen(arg), true);
        if (len != 0)
            tcp_raw_write(current_connection, arg, len);
    }
}

static void cmd_rawprint(char *arg)
{
    char *tmp = skipspace(arg);

    if (*tmp == '(') {
        ptr *pbuf = NULL;
        arg = tmp + 1;
        raw_escapes = true;
        evalp(&pbuf, &arg);
        raw_escapes = false;
        if (REAL_ERROR) {
            print_error(error);
            ptrdel(&pbuf);
        } else if (pbuf != NULL)
            tty_raw_write(ptrdata(pbuf), ptrlen(pbuf));
    } else {
        int len = memunescape(arg, strlen(arg), true);
        if (len != 0)
            tty_raw_write(arg, len);
    }
}


static void cmd_write(char *arg)
{
    ptr *p1 = NULL, *p2 = NULL;
    char *tmp = skipspace(arg);

    char kind = *tmp;
    if (kind == '!' || kind == '>')
        arg = ++tmp;
    else
        kind = 0;

    if (*tmp == '(') {
        arg = tmp + 1;
        evalp(&p1, &arg);
        if (REAL_ERROR)
            goto write_cleanup;

        if (*arg == CMDSEP) {
            ++arg;
            evalp(&p2, &arg);
            if (!REAL_ERROR && p2 == NULL)
                error = NO_STRING_ERROR;
            if (REAL_ERROR)
                goto write_cleanup;
        } else {
            message_printf("#write: ");
            error = SYNTAX_ERROR;
            goto write_cleanup;
        }
        if (*arg != ')') {
            message_printf("#write: ");
            error = MISSING_PAREN_ERROR;
            goto write_cleanup;
        }
        arg = ptrdata(p2);

        FILE *fp = (kind == '!'
                    ? shell_popen(arg, "w")
                    : fopen(arg, kind != 0 ? "w" : "a"));
        if (fp == NULL) {
            wrap_printf("#write: #error opening \"%s\"", arg);
            error = SYNTAX_ERROR;
            goto write_cleanup2;
        }
        fprintf(fp, "%s\n", p1 != NULL ? ptrdata(p1) : "");
        fflush(fp);
        if (kind == '!') pclose(fp); else fclose(fp);
    } else {
        message_printf("#write: ");
        error = MISMATCH_PAREN_ERROR;
    }

 write_cleanup:
    if (REAL_ERROR)
        print_error(error);
 write_cleanup2:
    ptrdel(&p1);
    ptrdel(&p2);
}

static void cmd_var(char *arg)
{
    arg = skipspace(arg);
    char *expr = first_regular(arg, '=');

    bool right = false, deleting = false;
    if (*expr != '\0') {
        *expr++ = '\0';     /* skip the = */
        strip_end(arg);     /* the spaces before it, as in @n = 5 */
        if (*expr == '\0')
            deleting = true;
        else {
            right = true;
            if (*expr == ' ')
                ++expr;
        }
    }

    enum var_kind kind;
    if (*arg == '$')
        kind = VAR_TEXT;
    else if (*arg == '@')
        kind = VAR_NUM;
    else if (*arg != '\0') {
        error = INVALID_NAME_ERROR;
        print_error(error);
        return;
    } else {
        show_vars();
        return;
    }

    varnode **p_named_var = NULL, *named_var = NULL;
    ptr *pbuf = NULL;
    int idx = 0;
    ++arg;
    if (isalpha((unsigned char)*arg) || *arg == '_') {
        /* found a named variable */
        char *tmp = arg;
        while (*tmp != '\0' && (isalnum((unsigned char)*tmp) || *tmp == '_'))
            ++tmp;
        if (*tmp != '\0') {
            error = INVALID_NAME_ERROR;
            print_error(error);
            return;
        }
        p_named_var = lookup_varnode(arg, kind);
        if (*p_named_var == NULL) {
            /* it doesn't (yet) exist */
            if (!deleting) {
                named_var = add_varnode(arg, kind);
                if (opt_info) {
                    wrap_printf("#new variable: \"%s\"", arg - 1);
                }
            } else {
                error = UNDEFINED_VARIABLE_ERROR;
                print_error(error);
                return;
            }
        } else
            named_var = *p_named_var;
    } else {
        /* not a named variable, may be
         * an unnamed variable or an expression */
        char *tmp = skipspace(arg);
        if (*tmp == '(') {
            /* an expression: its text, or a number, which evalp() writes
             * as text, is the name or the number of the variable */
            arg = tmp + 1;
            idx = evalp(&pbuf, &arg);
            if (REAL_ERROR) {
                message_printf("#var: ");
                print_error(error);
                ptrdel(&pbuf);
                return;
            }
            char *buf;
            if (pbuf != NULL)
                buf = ptrdata(pbuf);
            else {
                error = INVALID_NAME_ERROR;
                print_error(error);
                return;
            }
            char err_det;
            if (isdigit((unsigned char)*buf) || *buf == '-' || *buf == '+') {
                if (sscanf(buf, "%d%c", &idx, &err_det) == 1) {
                    if (idx < -GLOBAL_VAR_COUNT || idx >= PARAM_COUNT) {
                        error = OUT_RANGE_ERROR;
                        print_error(error);
                        ptrdel(&pbuf);
                        return;
                    }
                } else {
                    error = INVALID_NAME_ERROR;
                    print_error(error);
                    return;
                }
            } else {
                if (!isalpha((unsigned char)*buf) && *buf != '_') {
                    error = INVALID_NAME_ERROR;
                    print_error(error);
                    return;
                }
                tmp = buf + 1;
                while (*tmp != '\0'
                       && (isalnum((unsigned char)*tmp) || *tmp == '_'))
                    ++tmp;
                if (*tmp != '\0') {
                    error = INVALID_NAME_ERROR;
                    print_error(error);
                    return;
                }
                p_named_var = lookup_varnode(buf, kind);
                named_var = *p_named_var;
                if (named_var == NULL) {
                    if (!deleting) {
                        named_var = add_varnode(buf, kind);
                        if (opt_info) {
                            message_printf("#new variable: %c%s\n",
                                           kind == VAR_TEXT ? '$' : '@', buf);
                        }
                    } else {
                        error = UNDEFINED_VARIABLE_ERROR;
                        print_error(error);
                        ptrdel(&pbuf);
                        return;
                    }
                }
            }
        } else {
            /* an unnamed var */
            long buf2;

            idx = evall(&buf2, &arg);
            if (!REAL_ERROR && idx != TYPE_NUM)
                error = NO_STRING_ERROR;
            if (REAL_ERROR) {
                message_printf("#var: ");
                print_error(error);
                return;
            }
            idx = (int)buf2;
            if (idx < -GLOBAL_VAR_COUNT || idx >= PARAM_COUNT) {
                error = OUT_RANGE_ERROR;
                print_error(error);
                return;
            }
            /* ok, it's an unnamed var */
        }
    }

    long *var_num = named_var != NULL ? &named_var->num : num_var(idx);
    ptr **var_text = named_var != NULL ? &named_var->str : text_var(idx);

    if (kind == VAR_TEXT && right && *var_text == NULL) {
        *var_text = ptrnew(PARAMLEN);
        if (MEM_ERROR) {
            print_error(error);
            ptrdel(&pbuf);
            return;
        }
    }

    if (deleting) {
        /* R.I.P. named variables */
        if (named_var != NULL) {
            if (is_permanent_variable(named_var)) {
                wrap_printf("#cannot delete variable: \"%s\"", arg - 1);
            } else {
                delete_varnode(p_named_var, kind);
                if (opt_info) {
                    wrap_printf("#deleted variable: \"%s\"", arg - 1);
                }
            }
        } else if (kind == VAR_TEXT) {
        /* R.I.P. unnamed variables */
            if (*var_text != NULL) {
                if (idx < 0) {
                    ptrdel(var_text);
                    *var_text = NULL;
                } else
                    ptrzero(*var_text);
            }
        } else
            *var_num = 0;
        ptrdel(&pbuf);
        return;
    } else if (!right) {
        /* no right-hand expression, just show */
        if (named_var != NULL) {
            if (kind == VAR_TEXT) {
                pbuf = ptrescape(pbuf, *var_text, false);
                if (REAL_ERROR) {
                    print_error(error);
                    ptrdel(&pbuf);
                    return;
                }
                fmt_next_edit("#($%s = \"%s\")",
                              named_var->name,
                              pbuf != NULL ? ptrdata(pbuf) : "");
            } else {
                fmt_next_edit("#(@%s = %ld)",
                              named_var->name,
                              *var_num);
            }
        } else {
            if (kind == VAR_TEXT) {
                pbuf = ptrescape(pbuf, *var_text, false);
                fmt_next_edit("#($%d = \"%s\")", idx,
                              pbuf != NULL ? ptrdata(pbuf) : "");
            } else {
                fmt_next_edit("#(@%d = %ld)", idx,
                              *var_num);
            }
        }
        ptrdel(&pbuf);
        return;
    }

    /* only case left: assign a value to a variable */
    char from;
    long start, end;
    arg = redirect(expr, &pbuf, &from, "var", true, &start, &end);
    if (REAL_ERROR || arg == NULL)
        return;

    if (from != 0) {
        char buf2[BUFSIZE];
        FILE *fp = (from == '!') ? shell_popen(arg, "r") : fopen(arg, "r");
        if (fp == NULL) {
            wrap_printf("#var: #error opening \"%s\"", arg);
            error = SYNTAX_ERROR;
            ptrdel(&pbuf);
            return;
        }
        /* as much as fits in buf2: fgets reads nothing into a full one,
         * which would never end */
        int len = 0;
        long i = 1;
        while (error == 0 && (start == 0 || i <= end) && len < BUFSIZE - 1
               && utf8_fgets(buf2 + len, BUFSIZE - len, fp) != NULL)
            if (start == 0 || i++ >= start)
                len += strlen(buf2 + len);

        if (from == '!') pclose(fp); else fclose(fp);
        buf2[len] = '\0';
        arg = buf2;
    }

    if (kind == VAR_NUM) {
        arg = skipspace(arg);

        char *num_end;
        errno = 0;
        long num = strtol(arg, &num_end, 10);

        if (errno == ERANGE) {
            message_printf("#var: ");
            error = OUT_RANGE_ERROR;
            print_error(error);
        } else {
            if (num_end == arg) {
                message_printf("#var: ");
                error = NO_NUM_VALUE_ERROR;
                print_error(error);
            }
            *var_num = num;
        }
    } else {
        *var_text = ptrmcpy(*var_text, arg, strlen(arg));
        if (MEM_ERROR)
            print_error(error);
    }
    ptrdel(&pbuf);
}

static void cmd_setvar(char *arg)
{
    arg = skipspace(arg);
    char *name = arg;
    arg = first_regular(arg, '=');
    bool set = false;           /* else show */
    long buf;
    if (*arg != '\0') {
        *arg++ = '\0';
        if (*arg != '\0') {
            set = true;
            if (*arg == '(') {
                ++arg;
                int i = evall(&buf, &arg);
                if (!REAL_ERROR && i != TYPE_NUM)
                    error = NO_NUM_VALUE_ERROR;
                else if (!REAL_ERROR && *arg != ')')
                    error = MISSING_PAREN_ERROR;
            } else
                buf = strtol(arg, NULL, 0);
        } else
            buf = 0;

        if (REAL_ERROR) {
            message_printf("#setvar: ");
            print_error(error);
            return;
        }
    }

    strip_end(name);
    int i = strlen(name);
    if (i != 0 && strncmp(name, "timer", i) == 0) {
        update_now();
        if (!set)
            fmt_next_edit("#setvar timer=%ld",
                          diff_vtime(&now, &ref_time));
        else {
            vtime t;
            t.tv_usec = ((-buf) % mSEC_PER_SEC) * uSEC_PER_mSEC;
            t.tv_sec = (-buf) / mSEC_PER_SEC;
            ref_time.tv_usec = now.tv_usec;
            ref_time.tv_sec = now.tv_sec;
            add_vtime(&ref_time, &t);
        }
    } else if (i != 0 && strncmp(name, "lines", i) == 0) {
        if (!set)
            fmt_next_edit("#setvar lines=%d", lines);
        else if (buf < 1 || buf > INT_MAX) {
            message_printf("#lines must be from 1 to %d\n", INT_MAX);
        } else {
            lines = buf;
            if (opt_info) {
                message_printf("#setvar: lines=%d\n", lines);
            }
        }
    } else if (i != 0 && strncmp(name, "mem", i) == 0) {
        if (!set)
            fmt_next_edit("#setvar mem=%d", limit_mem);
        else {
            if (buf == 0 || buf >= PARAMLEN)
                limit_mem = buf <= INT_MAX ? (int)buf : INT_MAX;
            if (opt_info) {
                message_printf("#setvar: mem=%d%s\n", limit_mem,
                               limit_mem != 0 ? "" : " (unlimited)");
            }
        }
    } else if (i != 0 && strncmp(name, "buffer", i) == 0) {
        if (!set)
            fmt_next_edit("#setvar buffer=%d", log_getsize());
        else
            log_resize(buf);
    } else {
        update_now();
        message_printf("#setvar buffer=%d\n"
                       "#setvar lines=%d\n"
                       "#setvar mem=%d\n"
                       "#setvar timer=%ld\n",
                       log_getsize(), lines, limit_mem,
                       diff_vtime(&now, &ref_time));
    }
}

static void cmd_if(char *arg)
{
    arg = skipspace(arg);
    if (*arg != '(') {
        message_printf("#if: ");
        error = MISMATCH_PAREN_ERROR;
        print_error(error);
        return;
    }
    ++arg;  /* skip the '(' */

    long buf;
    int type = evall(&buf, &arg);
    if (!REAL_ERROR) {
        if (type != TYPE_NUM)
            error = NO_NUM_VALUE_ERROR;
        if (*arg != ')')
            error = MISSING_PAREN_ERROR;
        else {              /* skip the ')' */
            if (*++arg == ' ')
                ++arg;
        }
    }
    if (REAL_ERROR) {
        message_printf("#if: ");
        print_error(error);
        return;
    }

    if (buf != 0)
        parse_instruction(arg, false, false, true);
    else {
        arg = get_next_instr(arg);
        arg = skipspace(arg);
        if (strncmp(arg, "#else ", 6) == 0)
            parse_instruction(arg + 6, false, false, true);
    }
}

static void cmd_for(char *arg)
{
    arg = skipspace(arg);
    if (*arg != '(') {
        message_printf("#for: ");
        error = MISMATCH_PAREN_ERROR;
        print_error(error);
        return;
    }
    push_params();
    if (REAL_ERROR)
        return;

    arg = skipspace(arg + 1);    /* skip the '(' */
    if (*arg != CMDSEP)
        evaln(&arg);       /* execute <init> */

    char *check = arg + 1;
    int type = TYPE_NUM, loop = MAX_LOOP;
    long buf;
    char *increm = 0;

    if (REAL_ERROR)
        ;
    else if (*arg != CMDSEP) {
        message_printf("#for: ");
        error = MISSING_SEPARATOR_ERROR;
        print_error(error);
    } else while (error == 0 && loop != 0
                && (increm = check,
                    ((type = evall(&buf, &increm)) == TYPE_NUM
                     && error == 0 && *increm == CMDSEP && buf != 0))) {

        char *tmp = first_regular(increm + 1, ')');
        if (*tmp != '\0')
            parse_instruction(tmp + 1, true, true, true);
        else {
            message_printf("#for: ");
            error = MISSING_PAREN_ERROR;
            print_error(error);
        }

        if (error == 0) {
            tmp = increm + 1;
            if (*tmp != ')')
                evaln(&tmp);
        }

        --loop;
    }
    if (REAL_ERROR)
        ;
    else if (increm != NULL && *increm != CMDSEP)
        error = MISSING_SEPARATOR_ERROR;
    else if (loop == 0)
        error = MAX_LOOP_ERROR;
    else if (type != TYPE_NUM)
        error = NO_NUM_VALUE_ERROR;
    if (REAL_ERROR) {
        message_printf("#for: ");
        print_error(error);
    }
    if (error != DYN_STACK_UND_ERROR && error != DYN_STACK_OV_ERROR)
        pop_params();
}

static void cmd_while(char *arg)
{
    arg = skipspace(arg);
    if (*arg != '(') {
        message_printf("#while: ");
        error = MISMATCH_PAREN_ERROR;
        print_error(error);
        return;
    }
    push_params();

    char *check = ++arg;   /* skip the '(' */
    int type = TYPE_NUM, loop = MAX_LOOP;
    long buf;
    while (error == 0 && loop != 0
           && (arg = check,
               ((type = evall(&buf, &arg)) == TYPE_NUM
                && error == 0 && *arg == ')' && buf != 0))) {

        char *tmp = arg + 1;
        if (*tmp == ' ')          /* skip the ')' */
            ++tmp;
        if (*tmp != '\0')
            parse_instruction(tmp, true, true, true);
        --loop;
    }
    if (REAL_ERROR)
        ;
    else if (*arg != ')')
        error = MISSING_PAREN_ERROR;
    else if (loop == 0)
        error = MAX_LOOP_ERROR;
    else if (type != TYPE_NUM)
        error = NO_NUM_VALUE_ERROR;
    if (REAL_ERROR) {
        message_printf("#while: ");
        print_error(error);
    }
    if (error != DYN_STACK_UND_ERROR && error != DYN_STACK_OV_ERROR)
        pop_params();
}

static void cmd_capture(char *arg)
{
    static char name[BUFSIZE];  /* of the file, for the end */
    arg = skipspace(arg);
    strip_end(arg);        /* such as after Tab completion */

    if (*arg == '\0') {
        if (capturefile != NULL) {
            log_flush();
            fclose(capturefile);
            capturefile = NULL;
            if (opt_info) {
                wrap_printf("#end of capture to \"%s\".", name);
            }
        } else {
            wrap_printf("#capture to what file?");
        }
    } else {
        if (capturefile != NULL) {
            message_printf("#capture already active.\n");
        } else {
            bool append = false;
            /* Append to log file, if the name starts with '>' */
            if (*arg == '>') {
                ++arg;
                append = true;
            }
            capturefile = fopen(arg, append ? "a" : "w");
            if (capturefile == NULL) {
                wrap_printf("#error writing file \"%s\"", arg);
            } else {
                snprintf(name, sizeof(name), "%s", arg);
                if (opt_info)
                    wrap_printf("#capture to \"%s\" active, \"#capture\""
                                " ends.", arg);
            }
        }
    }
}

static void cmd_movie(char *arg)
{
    static char name[BUFSIZE];  /* of the file, for the end */
    arg = skipspace(arg);
    strip_end(arg);        /* such as after Tab completion */

    if (*arg == '\0') {
        if (moviefile != NULL) {
            log_flush();
            fclose(moviefile);
            moviefile = NULL;
            if (opt_info) {
                wrap_printf("#end of movie to \"%s\".", name);
            }
        } else {
            wrap_printf("#movie to what file?");
        }
    } else {
        if (moviefile != NULL) {
            message_printf("#movie already active.\n");
        } else {
            moviefile = fopen(arg, "w");
            if (moviefile == NULL) {
                wrap_printf("#error writing file \"%s\"", arg);
            } else {
                if (opt_info) {
                    wrap_printf("#movie to \"%s\" active, \"#movie\" ends.",
                                arg);
                }
                snprintf(name, sizeof(name), "%s", arg);
                update_now();
                movie_last = now;
                log_clearsleep();
            }
        }
    }
}

static void cmd_record(char *arg)
{
    static char name[BUFSIZE];  /* of the file, for the end */
    arg = skipspace(arg);
    strip_end(arg);        /* such as after Tab completion */

    if (*arg == '\0') {
        if (recordfile != NULL) {
            fclose(recordfile);
            recordfile = NULL;
            if (opt_info) {
                wrap_printf("#end of record to \"%s\".", name);
            }
        } else {
            wrap_printf("#record to what file?");
        }
    } else {
        if (recordfile != NULL) {
            message_printf("#record already active.\n");
        } else {
            recordfile = fopen(arg, "w");
            if (recordfile == NULL) {
                wrap_printf("#error writing file \"%s\"", arg);
            } else {
                snprintf(name, sizeof(name), "%s", arg);
                if (opt_info)
                    wrap_printf("#record to \"%s\" active, \"#record\" ends.",
                                arg);
            }
        }
    }
}

static void cmd_edit(char *arg)
{
    if (edit_sess != NULL) {
        for (editsess *sp = edit_sess; sp != NULL; sp = sp->next) {
            message_printf("# %s (%u)\n", sp->descr, sp->key);
        }
    } else {
        message_printf("#no active editors.\n");
    }
}

static void cmd_cancel(char *arg)
{
    arg = skipspace(arg);
    char *end = arg;
    unsigned long key = 0;
    bool too_large = false;
    if (isdigit((unsigned char)*arg)) {
        errno = 0;
        key = strtoul(arg, &end, 10);
        too_large = errno == ERANGE;
    }
    if (*skipspace(end) != '\0') {
        message_printf("#syntax: #cancel [<number>]\n");
    } else if (edit_sess == NULL) {
        message_printf("#no editing sessions to cancel.\n");
    } else if (end != arg) {
        editsess *sp = too_large ? NULL : edit_sess;
        while (sp != NULL && sp->key != key) {
            sp = sp->next;
        }
        if (sp == NULL) {
            message_printf("#unknown editing session %.*s\n",
                           (int)(end - arg), arg);
        } else if (sp->cancel) {
            message_printf("#editing session %u already cancelled\n",
                           sp->key);
        } else {
            cancel_edit(sp);
        }
    } else {
        bool cancelled = false;
        for (editsess *sp = edit_sess; sp != NULL; sp = sp->next) {
            if (!sp->cancel) {
                cancel_edit(sp);
                cancelled = true;
            }
        }
        if (!cancelled) {
            message_printf("#no editing sessions to cancel.\n");
        }
    }
}

static void cmd_net(char *arg)
{
    wrap_printf("#received from host: %ld chars, sent to host: %ld chars.",
                received, sent);
}

static void cmd_cpu(char *arg)
{
    update_now();
    cpu_clock = clock();
    float f = (float)(cpu_clock - start_clock) / (float)CLOCKS_PER_SEC;
    float l = (float)(diff_vtime(&now, &start_time)) / (float)mSEC_PER_SEC;
    message_printf("#CPU time used: %.3f sec. (%.2f%%)\n", f,
                   (l > 0 && l > f) ? f * 100.0 / l : 100.0);
}

/*
 * consume any --tls, --no-tls, --utf8 and --latin1 flags starting at
 * *argp, leaving *argp at the next non-flag argument; *tls is set to 1
 * for --tls and 0 for --no-tls, *utf8 to 1 for --utf8 and 0 for --latin1
 */
static bool maybe_flag_args(char **argp, struct tls_options *tlsopt,
                            int *tls, int *utf8)
{
    while (*argp != NULL && **argp != '\0') {
        bool u;
        if (parse_charset_flag(*argp, &u)) {
            *utf8 = u;
        } else {
            if (*tls >= 0)
                return true;

            if (strcmp(*argp, NO_TLS_FLAG) == 0) {
                *tls = 0;
            } else {
                const char *targ = *argp;
                if (!parse_tls_flag(tlsopt, &targ, true)) {
                    message_printf("#connect: invalid argument '%s'\n", *argp);
                    return false;
                }
                if (!tlsopt->use_tls)
                    return true;
                *tls = 1;
            }
        }
        *argp = strtok(NULL, " ");
    }
    return true;
}

static void cmd_connect(char *arg)
{
    if (*skipspace(arg) == '\0') {
        tcp_show();
        return;
    }

    struct tls_options tlsopt = { .use_tls = false };
    int tls = -1;               /* no --tls or --no-tls */
    int utf8 = -1;              /* no --utf8 or --latin1 */

    int argc = 1;
    char *s1 = strtok(arg, " ");
    if (!maybe_flag_args(&s1, &tlsopt, &tls, &utf8))
        return;

    char *s2 = strtok(NULL, " ");
    if (!maybe_flag_args(&s2, &tlsopt, &tls, &utf8))
        return;

    char *s3 = NULL, *s4 = NULL;

    if (s2 != NULL && *s2 != '\0') {
        ++argc;
        s3 = strtok(NULL, " ");
        if (!maybe_flag_args(&s3, &tlsopt, &tls, &utf8))
            return;
        if (s3 != NULL && *s3 != '\0') {
            ++argc;
            s4 = strtok(NULL, " ");
            if (!maybe_flag_args(&s4, &tlsopt, &tls, &utf8))
                return;
            if (s4 != NULL && *s4 != '\0')
                ++argc;
        }
    }

    if (argc <= 2) {
        if (*hostinfo.name != '\0')
            tcp_open(s1, s2, hostinfo.name, hostinfo.port,
                     tls >= 0 ? &tlsopt : &hostinfo.tlsopt,
                     (utf8 >= 0 ? utf8 : hostinfo.utf8) != 0);
        else {
            message_printf("#connect: no host defined!\n"
                           "#syntax: #connect"
                           " <session-id> [<initstr>] <address> <port>"
                           " [--tls[=<options>]|" NO_TLS_FLAG "]"
                           " [" UTF8_FLAG "|" LATIN1_FLAG "]\n");
        }
    } else if (argc == 3) {
        if (tls < 0)
            tlsopt.use_tls = default_tls;
        if (utf8 < 0)
            utf8 = default_utf8;
        if (*hostinfo.name == '\0') {
            my_strncpy(hostinfo.name, s2, BUFSIZE - 1);
            hostinfo.port = atoi(s3);
            hostinfo.tlsopt = tlsopt;
            hostinfo.utf8 = utf8 != 0;
        }
        tcp_open(s1, NULL, s2, atoi(s3), &tlsopt, utf8 != 0);
    } else {
        char *s5 = strtok(NULL, " ");
        if (!maybe_flag_args(&s5, &tlsopt, &tls, &utf8))
            return;
        if (tls < 0)
            tlsopt.use_tls = default_tls;
        if (utf8 < 0)
            utf8 = default_utf8;

        if (*hostinfo.name == '\0') {
            my_strncpy(hostinfo.name, s3, BUFSIZE - 1);
            hostinfo.port = atoi(s4);
            hostinfo.tlsopt = tlsopt;
            hostinfo.utf8 = utf8 != 0;
        }
        tcp_open(s1, s2, s3, atoi(s4), &tlsopt, utf8 != 0);
    }
}


static void cmd_spawn(char *arg)
{
    arg = skipspace(arg);
    if (*arg != '\0') {
        char s[BUFSIZE];
        arg = split_first_word(s, BUFSIZE, arg);
        if (*arg != '\0' && *s != '\0') {
            tcp_spawn(s, arg);
            return;
        }
    }
    message_printf("#syntax: #spawn <session-id> <command>\n");
}

/* If you have speedwalk off but still want to use a speedwalk sequence,
 * you can manually trigger a speedwalk this way */
static void cmd_speedwalk(char *arg)
{
    bool save_speedwalk = opt_speedwalk;
    message_printf("Executing speedwalk '%s'\n", arg);
    opt_speedwalk = true;
    if (!map_walk(skipspace(arg), false, false)) {
        message_printf("Error executing speedwalk\n");
    }
    opt_speedwalk = save_speedwalk;
}

static void cmd_zap(char *arg)
{
    if (*arg == '\0') {
        message_printf("#zap: no connection name.\n");
    } else
        tcp_close(arg);
}

static void cmd_qui(char *arg)
{
    wrap_printf("#you have to write '#quit' - no less, to quit!");
}

static void cmd_quit(char *arg)
{
    if (*arg != '\0') { /* no skipspace() here! */
        message_printf("#quit: spurious argument?\n");
    } else
        exit_powwow();
}

static const struct {
    const char *name;
    bool *option;
    const char *doc;
} options[] = {
    { .name = "autoclear", .option = &opt_autoclear,
      .doc = "clear input line before executing commands" },
    { .name = "autoprint", .option = &opt_autoprint,
      .doc = "#print lines matched by actions" },
    { .name = "compact", .option = &opt_compact,
      .doc = "remove prompt when receiving new messages from mud" },
    { .name = "debug", .option = &opt_debug,
      .doc = "print commands before executing" },
    { .name = "echo", .option = &opt_echo,
      .doc = "print command action commands when executed" },
    { .name = "exit", .option = &opt_exit,
      .doc = "automatically exit powwow when mud connection closes" },
    { .name = "history", .option = &opt_history,
      .doc = "also save command history" },
    { .name = "info", .option = &opt_info,
      .doc = "print information about command effects" },
    { .name = "keyecho", .option = &opt_keyecho,
      .doc = "print command bound to key when executed" },
    { .name = "reprint", .option = &opt_reprint,
      .doc = "reprint sent commands when getting new prompt" },
    { .name = "sendsize", .option = &opt_sendsize,
      .doc = "send terminal size when opening connection" },
    { .name = "speedwalk", .option = &opt_speedwalk,
      .doc = "enable speed walking (ness3ew...)" },
    { .name = "words", .option = &opt_words,
      .doc = "also save word history" },
    { .name = "wrap", .option = &opt_wrap,
      .doc = "enable word wrapping" },
    { 0 }
};

char *opt_editor, *opt_pager, *opt_shell;

/* the options that are commands, set as #option <name>=<command> */
static const struct {
    const char *name;
    char **value;
    const char *doc;
} command_options[] = {
    { .name = "editor", .value = &opt_editor,
      .doc = "edit texts of the MUD with this, not $POWWOWEDITOR" },
    { .name = "pager", .value = &opt_pager,
      .doc = "show texts, such as #help, with this, not $POWWOWPAGER" },
    { .name = "shell", .value = &opt_shell,
      .doc = "run #! and ! commands with this, not $POWWOWSHELL" },
    { 0 }
};

/* write "#option <name>=<command>" for the command options set to 'file',
 * or the tty if it is NULL: as typed, with its escapes, which #option
 * keeps; return -1 on error */
static int print_command_options(FILE *file)
{
    for (int i = 0; command_options[i].name != NULL; ++i) {
        const char *value = *command_options[i].value;
        if (value == NULL)
            continue;
        int res = (file != NULL
                   ? fprintf(file, "#option %s=%s\n",
                             command_options[i].name, value)
                   : tty_printf("#option %s=%s\n",
                                command_options[i].name, value));
        if (res < 0)
            return -1;
    }
    return 1;
}

/* print all options to 'file', or tty if file is NULL; return -1 on
 * error, 1 on success */
int print_all_options(FILE *file)
{
    const char *prefix = "#option";
    int width = (file != NULL ? WRAP_COLUMNS : wrap_width() + 1) - 16;
    int len = 0;
    for (int i = 0; options[i].name != NULL; ++i) {
        int res;
        if (file != NULL)
            res = fprintf(file, "%s %c%s", prefix,
                          *options[i].option ? '+' : '-',
                          options[i].name);
        else
            res = tty_printf("%s %c%s", prefix,
                             *options[i].option ? '+' : '-',
                             options[i].name);
        if (res < 0)
            return -1;
        /* don't rely on printf() return value */
        len += strlen(prefix) + strlen(options[i].name) + 2;
        if (len >= width) {
            prefix = "\n#option";
            len = -1;
        } else {
            prefix = "";
        }
    }
    if (file != NULL)
        fputc('\n', file);
    else
        tty_putc('\n');
    if (print_command_options(file) < 0)
        return -1;
    if (file == NULL)
        status(1);
    return 1;
}

const char *option_name(int i)
{
    return options[i].name;
}

/* the name of command option 'i', or NULL after the last */
const char *command_option_name(int i)
{
    return command_options[i].name;
}

/*
 * #option <name>=[<command>] and #option =<name>, if 'word', of 'len'
 * bytes, names a command option: set it to 'rest', the text after the
 * '=' to the end of the line, or clear it if that is empty; or put it in
 * the input line to edit; return false if 'word' is not one
 */
static bool command_option(const char *word, int len, const char *rest)
{
    bool edit = word[0] == '=';
    if (edit) {
        ++word;
        --len;
    }
    /* =editor= too, as Tab completes =ed */
    if (edit && len > 0 && word[len - 1] == '=')
        --len;
    const char *eq = memchr(word, '=', len);
    int name_len = eq != NULL ? (int)(eq - word) : len;
    if (edit == (eq != NULL) || name_len <= 0)
        return false;
    int i = 0;
    while (command_options[i].name != NULL
           && (strncmp(command_options[i].name, word, name_len) != 0
               || command_options[i].name[name_len] != '\0'))
        ++i;
    if (command_options[i].name == NULL)
        return false;

    char **value = command_options[i].value;
    if (edit) {
        fmt_next_edit("#option %s=%s", command_options[i].name,
                      *value != NULL ? *value : "");
        return true;
    }
    free(*value);
    *value = NULL;
    if (*rest != '\0') {
        *value = xstrdup(rest);
    }
    if (opt_info) {
        if (*value != NULL)
            message_printf("#option %s is now: %s\n", command_options[i].name,
                           *value);
        else
            message_printf("#option %s is now not set.\n",
                           command_options[i].name);
    }
    return true;
}

static void cmd_option(char *arg)
{
    arg = skipspace(arg);
    if (*arg == '\0') {
        print_all_options(NULL);
        return;
    }

    char buf[BUFSIZE];
    int count = 0;
    for (;;) {
        /* the command of a command option is the rest of the line */
        const char *word = arg;
        int word_len = (int)strcspn(word, " ");
        const char *eq = memchr(word, '=', word_len);
        if (eq != NULL
            && command_option(word, word_len,
                              word[0] == '=' ? "" : eq + 1)) {
            return;
        }
        arg = skipspace(split_first_word(buf, BUFSIZE, arg));
        if (*buf == '\0')
            break;
        enum { MODE_ON, MODE_OFF, MODE_TOGGLE, MODE_REP } mode;
        char *p = buf;
        char c = *p;
        int len = strlen(p);

        switch (c) {
        case '=': mode = MODE_REP; ++p; break;
        case '+': mode = MODE_ON;  ++p; break;
        case '-': mode = MODE_OFF; ++p; break;
        default:  mode = MODE_TOGGLE;     break;
        }
        ++count;
        bool *varp = NULL;
        int i;
        for (i = 0; options[i].name != NULL; ++i) {
            if (strncmp(options[i].name, p, len) == 0) {
                varp = options[i].option;
                break;
            }
        }
        if (varp == NULL) {
            if (strncmp("list", p, len) == 0) {
                tty_message("#list of options:\n");
                for (i = 0; options[i].name != NULL; ++i) {
                    assert(options[i].option != NULL);
                    tty_message("#option %c%-12s    %s\n",
                               *options[i].option ? '+' : '-',
                               options[i].name,
                               options[i].doc);
                }
                for (i = 0; command_options[i].name != NULL; ++i) {
                    const char *value = *command_options[i].value;
                    tty_message("#option %s=%-*s %s\n",
                                command_options[i].name,
                                (int)(12 - strlen(command_options[i].name)),
                                value != NULL ? value : "",
                                command_options[i].doc);
                }
            } else {
                tty_message("#syntax: #option [[+|-|=]<option>|list]..."
                            " | <option>=[<command>]\n");
            }
            status(1);
            return;
        }

        switch (mode) {
        case MODE_REP:
            fmt_next_edit("#option %c%s", *varp ? '+' : '-', p);
            break;
        case MODE_ON:     *varp = true; break;
        case MODE_OFF:    *varp = false; break;
        case MODE_TOGGLE: *varp = !*varp; break;
        }
        /*
         * reset the reprint buffer if changing its status
         */
        if (varp == &opt_reprint)
            reprint_clear();

        /* as above, but always print status if
         * "#option info" alone was typed */
        if (mode != MODE_REP && *arg == '\0' && count == 1
            && (opt_info || (mode == MODE_TOGGLE && varp == &opt_info))) {
            message_printf("#option %s is now o%s.\n",
                           options[i].name,
                           *varp ? "n" : "ff");
        }
    }
}

static void cmd_file(char *arg)
{
    arg = skipspace(arg);
    strip_end(arg);        /* such as after Tab completion */
    if (*arg == '=') {
        set_deffile(++arg);
        if (opt_info) {
            if (*arg != '\0') {
                wrap_printf("#save-file set to \"%s\"", deffile);
            } else {
                wrap_printf("#save-file is now undefined.");
            }
        }
    } else if (*deffile != '\0') {
        fmt_next_edit("#file =%s", deffile);
    } else {
        wrap_printf("#save-file not defined.");
    }
}

static void cmd_save(char *arg)
{
    arg = skipspace(arg);
    strip_end(arg);        /* such as after Tab completion */
    if (*arg != '\0') {
        set_deffile(arg);
        if (opt_info) {
            wrap_printf("#save-file set to \"%s\"", deffile);
        }
    } else if (*deffile == '\0') {
        wrap_printf("#save-file not defined.");
        return;
    }

    if (*deffile != '\0' && save_settings() > 0 && opt_info) {
        wrap_printf("#settings saved to \"%s\".", deffile);
    }
}

static void cmd_load(char *arg)
{
    arg = skipspace(arg);
    strip_end(arg);        /* such as after Tab completion */
    if (*arg != '\0') {
        set_deffile(arg);
        if (opt_info) {
            wrap_printf("#save-file set to \"%s\"", deffile);
        }
    } else if (*deffile == '\0') {
        wrap_printf("#save-file not defined.");
        return;
    }

    int res = read_settings();

    if (res > 0) {
        if (opt_info) {
            wrap_printf("#settings loaded from file.");
        }
    } else if (res < 0) {
        /* critical error */
        while (keydefs != NULL)
            delete_keynode(&keydefs);
        tty_add_initial_binds();
        tty_add_walk_binds();
        limit_mem = 1048576;
        message_printf("#emergency loaded default settings.\n");
    }
}

static char *trivial_eval(ptr **pbuf, char *arg, const char *name)
{
    if (pbuf == NULL)
        return NULL;

    char *tmp = skipspace(arg);
    if (*tmp == '(') {
        arg = tmp + 1;
        evalp(pbuf, &arg);
        if (!REAL_ERROR && *arg != ')')
            error = MISSING_PAREN_ERROR;
        if (REAL_ERROR) {
            message_printf("#%s: ", name);
            print_error(error);
            return NULL;
        }
        if (*pbuf != NULL)
            arg = ptrdata(*pbuf);
        else
            arg += strlen(arg); /* the empty string at its end */
    } else
        unescape(arg);

    return arg;
}

static void do_cmd_add(char *arg, bool is_static)
{
    ptr *pbuf = NULL;

    arg = trivial_eval(&pbuf, arg, "add");
    if (!REAL_ERROR)
        while (*arg != '\0') {
            char buf[BUFSIZE];
            arg = split_first_word(buf, BUFSIZE, arg);
            if (strlen(buf) >= MIN_WORDLEN)
                (is_static ? put_static_word : put_word)(buf);
        }
    ptrdel(&pbuf);
}

static void cmd_add(char *arg)
{
    do_cmd_add(arg, false);
}

static void cmd_addstatic(char *arg)
{
    do_cmd_add(arg, true);
}

static void cmd_put(char *arg)
{
    ptr *pbuf = NULL;
    arg = trivial_eval(&pbuf, arg, "put");
    if (!REAL_ERROR && *arg != '\0')
        put_history(arg);
    ptrdel(&pbuf);
}


