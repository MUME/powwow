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
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#ifdef HAVE_LIBDL
#include <dlfcn.h>
#endif

#include "defines.h"
#include "main.h"
#include "utils.h"
#include "beam.h"
#include "cmd.h"
#include "cmd2.h"
#include "edit.h"
#include "list.h"
#include "map.h"
#include "tcp.h"
#include "tty.h"
#include "eval.h"
#include "log.h"

/*           local function declarations            */
#define _  __P ((char *arg))

static void cmd_help _, cmd_shell _, cmd_action _, cmd_add _,
  cmd_addstatic _, cmd_alias _, cmd_at _, cmd_beep _, cmd_bind _,
  cmd_cancel _, cmd_capture _, cmd_clear _, cmd_connect _, cmd_cpu _,
  cmd_do _, cmd_delim _, cmd_edit _, cmd_emulate _, cmd_exe _,
  cmd_file _, cmd_for _, cmd_hilite _, cmd_history _, cmd_host _,
  cmd_identify _, cmd_if _, cmd_in _, cmd_init _, cmd_isprompt _,
  cmd_key _, cmd_keyedit _,
  cmd_load _, cmd_map _, cmd_mark _, cmd_movie _,
  cmd_net _, cmd_nice _, cmd_option _,
  cmd_prefix _, cmd_print _, cmd_prompt _, cmd_put _,
  cmd_qui _, cmd_quit _, cmd_quote _,
  cmd_rawsend _, cmd_rawprint _, cmd_rebind _, cmd_rebindall _, cmd_rebindALL _,
  cmd_record _, cmd_request _, cmd_reset _, cmd_retrace _,
  cmd_save _, cmd_send _, cmd_setvar _, cmd_snoop _, cmd_spawn _, cmd_stop _,
  cmd_time _, cmd_var _, cmd_ver _, cmd_while _, cmd_write _,
  cmd_eval _, cmd_zap _, cmd_module _, cmd_group _, cmd_speedwalk _, cmd_groupdelim _;

#ifdef BUG_TELNET
static void cmd_color _;
#endif

#undef _

/* This must be init'd now at runtime */
cmdstruct *commands = NULL;

#define C(name, func, help) { NULL, name, help, func, NULL }

/* The builtin commands */
cmdstruct default_commands[] =
{
    C("help",       cmd_help,
      "[keys|math|command]\tthis text, or help on specified topic"),
    C("17", (function_str)0,
      "command\t\t\trepeat \"command\" 17 times"),
#ifndef NO_SHELL
    C("!",          cmd_shell,
      "shell-command\t\texecute a shell command using /bin/sh"),
#endif
    C("action",     cmd_action,
      "[[<|=|>|%][+|-]name] [{pattern|(expression)} [=[command]]]\n"
      "\t\t\t\tdelete/list/define actions"),
    C("add",        cmd_add,
      "{string|(expr)}\t\tadd the string to word completion list"),
    C("addstatic",  cmd_addstatic,
      "{string|(expr)}\t\tadd the string to the static word completion list"),
    C("alias",      cmd_alias,
      "[name[=[text]]]\t\tdelete/list/define aliases"),
    C("at",         cmd_at,
      "[name [(time-string) [command]]\n\t\t\t\tset time of delayed label"),
    C("beep",       cmd_beep,
      "\t\t\t\tmake your terminal beep (like #print (*7))"),
    C("bind",       cmd_bind,
      "[edit|name [seq][=[command]]]\n"
      "\t\t\t\tdelete/list/define key bindings"),
    C("cancel",     cmd_cancel,
      "[number]\t\tcancel editing session"),
    C("capture",    cmd_capture,
      "[filename]\t\tbegin/end of capture to file"),
    C("clear",      cmd_clear,
      "\t\t\tclear input line (use from spawned programs)"),
#ifdef BUG_TELNET
    C("color",      cmd_color,
      "attr\t\t\tset default colors/attributes"),
#endif
    C("connect",    cmd_connect,
      "[connect-id [initstr] [address port]\n"
      "\t\t\t\topen a new connection"),
    C("cpu",        cmd_cpu,
      "\t\t\t\tshow CPU time used by powwow"),
    C("delim",      cmd_delim,
      "[normal|program|{custom [chars]}]\n"
      "\t\t\t\tset word completion delimeters"),
    C("do",         cmd_do,
      "(expr) command\t\trepeat \"command\" (expr) times"),
    C("edit",       cmd_edit,
      "\t\t\t\tlist editing sessions"),
    C("emulate",    cmd_emulate,
      "[<|!]{text|(expr)}\tprocess result as if received from host"),
    C("exe",        cmd_exe,
      "[<|!]{text|(string-expr)}\texecute result as if typed from keyboard"),
    C("file",       cmd_file,
      "[=[filename]]\t\tset/show powwow definition file"),
    C("for",        cmd_for,
      "([init];check;[loop]) command\n"
      "\t\t\t\twhile \"check\" is true exec \"command\""),
    C("group",      cmd_group,
      "[name] [on|off|list]\tgroup alias/action manipulation"),
    C("groupdelim", cmd_groupdelim,
      "[delimiter]\tchange delimiter for action/alias groups"),
    C("hilite",     cmd_hilite,
      "[attr]\t\t\thighlight your input line"),
    C("history",    cmd_history,
      "[{number|(expr)}]\tlist/execute commands in history"),
    C("host",       cmd_host,
      "[hostname port]]\tset/show address of default host"),
    C("identify",   cmd_identify,
      "[startact [endact]]\tsend MUME client identification"),
    C("if",         cmd_if,
      "(expr) instr1 [; #else instr2]\n"
      "\t\t\t\tif \"expr\" is true execute \"instr1\",\n"
      "\t\t\t\totherwise execute \"instr2\""),
    C("in",         cmd_in,
      "[label [(delay) [command]]]\tdelete/list/define delayed labels"),
    C("init",       cmd_init,
      "[=[command]]\t\tdefine command to execute on connect to host"),
    C("isprompt",   cmd_isprompt,
      "\t\t\trecognize a prompt as such"),
    C("key",        cmd_key,
      "name\t\t\texecute the \"name\" key binding"),
    C("keyedit",    cmd_keyedit,
      "editing-name\t\trun a line-editing function"),
    C("load",       cmd_load,
      "[filename]\t\tload powwow settings from file"),
    C("map",        cmd_map,
      "[-[number]|walksequence]\tshow/clear/edit (auto)map"),
    C("mark",       cmd_mark,
      "[string[=[attr]]]\t\tdelete/list/define markers"),
#ifdef HAVE_LIBDL
    C("module",     cmd_module,
      "[name]\t\t\tload shared library extension"),
#endif
    C("movie",      cmd_movie,
      "[filename]\t\tbegin/end of movie record to file"),
    C("net",        cmd_net,
      "\t\t\t\tprint amount of data received from/sent to host"),
    C("nice",       cmd_nice,
      "[{number|(expr)}[command]]\n"
      "\t\t\t\tset/show priority of new actions/marks"),
    C("option",     cmd_option,
      "[[+|-|=]name]|list\tlist or view various options"),
    C("prefix",     cmd_prefix,
      "string\t\t\tprefix all lines with string"),
    C("",           cmd_eval,
      "(expr)\t\t\tevaluate expression, trashing result"),
    C("print",      cmd_print,
      "[<|!][text|(expr)]\tprint text/result on screen, appending a \\n\n"
      "\t\t\t\tif no argument, prints value of variable $0"),
    C("prompt",     cmd_prompt,
      "[[<|=|>|%][+|-]name] [{pattern|(expression)} [=[prompt-command]]]\n"
      "\t\t\t\tdelete/list/define actions on prompts"),
    C("put",        cmd_put,
      "{text|(expr)}\t\tput text/result of expression in history"),
    C("qui",        cmd_qui,
      "\t\t\t\tdo nothing"),
    C("quit",       cmd_quit,
      "\t\t\t\tquit powwow"),
    C("quote",      cmd_quote,
      "[on|off]\t\t\ttoggle verbatim-flag on/off"),
    C("rawsend",    cmd_rawsend,
      "{string|(expr)}\tsend raw data to the MUD"),
    C("rawprint",   cmd_rawprint,
      "{string|(expr)}\tsend raw data to the screen"),
    C("rebind",     cmd_rebind,
      "name [seq]\t\tchange sequence of a key binding"),
    C("rebindall",  cmd_rebindall,
      "\t\t\trebind all key bindings"),
    C("rebindALL",  cmd_rebindALL,
      "\t\t\trebind ALL key bindings, even trivial ones"),
    C("record",     cmd_record,
      "[filename]\t\tbegin/end of record to file"),
    C("request",    cmd_request,
      "[editor][prompt][all]\tsend various identification strings"),
    C("reset",      cmd_reset,
      "<list-name>\t\tclear the whole defined list and reload default"),
    C("retrace",    cmd_retrace,
      "[number]\t\tretrace the last number steps"),
    C("save",       cmd_save,
      "[filename]\t\tsave powwow settings to file"),
    C("send",       cmd_send,
      "[<|!]{text|(expr)}\teval expression, sending result to the MUD"),
    C("setvar",     cmd_setvar,
      "name[=text|(expr)]\tset/show internal limits and variables"),
    C("snoop",      cmd_snoop,
      "connect-id\t\ttoggle output display for connections"),
    C("spawn",      cmd_spawn,
      "connect-id command\ttalk with a shell command"),
    C("speedwalk",  cmd_speedwalk,
      "[speedwalk sequence]\texecute a speedwalk sequence explicitly"),
    C("stop",       cmd_stop,
      "\t\t\t\tremove all delayed commands from active list"),
    C("time",       cmd_time,
      "\t\t\t\tprint current time and date"),
    C("var",        cmd_var,
      "variable [= [<|!]{string|(expr)} ]\n"
      "\t\t\t\twrite result into the variable"),
    C("ver",        cmd_ver,
      "\t\t\t\tshow powwow version"),
    C("while",      cmd_while,
      "(expr) instr\t\twhile \"expr\" is true execute \"instr\""),
    C("write",      cmd_write,
      "[>|!](expr;name)\t\twrite result of expr to \"name\" file"),
    C("zap",        cmd_zap,
      "connect-id\t\t\tclose a connection"),
    { NULL }
};

char *_cmd_sort_name( cmdstruct *cmd ) {
	if( cmd -> sortname == NULL )
		return( cmd -> name );
	else
		return( cmd -> sortname );
}

/* Adds a cmd to commands (inserts the ptr in the list, DO NOT FREE IT) */
void cmd_add_command( cmdstruct *cmd ) {
	/* do insert/sort */
	cmdstruct *c = commands;

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
	for( c = commands; c != NULL; c = c -> next ) {
		if( strcmp( cmd -> name, c -> name ) == 0 ) {
			PRINTF( "#error %s is already defined\n", c -> name );
			return;
		}
	}

	
	/* catch insertion to head of list */
	if( commands == NULL ) {
		/* no commands yet */
		commands = cmd;
		cmd -> next = NULL;
		return;
	}

	if( strcmp( _cmd_sort_name( commands ), _cmd_sort_name( cmd ) ) > 0 ) {
		/* this is lower in sort than every item, so
		 * make it the head of the list */
		cmd -> next = commands;
		commands = cmd;
		return;
	}

	for( c = commands; c != NULL; c = c -> next ) {
		if( strcmp( _cmd_sort_name( cmd ), _cmd_sort_name( c ) ) >= 0 ) {
			/* Need second check to handle empty string case */
			if( c -> next == NULL || strcmp( _cmd_sort_name( cmd ), _cmd_sort_name( c -> next ) ) <= 0 ) {
				/*PRINTF( "Inserting %s after %s\n", cmd -> name, c -> name ); */

				/* insert after this one, it is greater than this
				 * entry but less than the next */
				cmd -> next = c -> next;
				c -> next = cmd;
				return;
			}
		}
	}

	PRINTF( "ERROR INSERTING COMMAND\n" );
}

/* Init the command listing, called from main */
void initialize_cmd(void) {
	int i;

	/* Now add the default command list */
	for( i = 0; default_commands[ i ].name; i++ )
		cmd_add_command( &default_commands[ i ] );
}

#ifdef HAVE_LIBDL
static void cmd_module __P1 (char *,arg) {
	char libname[1024];
	void *lib;
	void (*func)();

	int pindex;
	struct stat junk;
	char *prefixes[] = {
		PLUGIN_DIR,
		".",
		"/lib/powwow",
		"/usr/lib/powwow",
		"/usr/local/lib/powwow",
		"$HOME/.powwow/lib" /* this doesn't work, but is here to remind me :p */
	};

	arg = skipspace(arg);

	/* I changed it to work this way so that you can have libs in multiple places and
	 * also eventually to allow it to use .dll instead of .so under the cygwin environment */
	for( pindex = 0; pindex < 5; pindex++ ) {
		memset( libname, 0, sizeof libname );

        /* don't look for name without .so, it breaks if you have a file
         * with the same name in the current dir and making it .so for sure
         * will skip these files since they are probably not libs to load
		snprintf( libname, 1024, "%s/%s", prefixes[ pindex ], arg );
		if( stat( libname, &junk ) == 0 ) {
			break;
		}
        */

		snprintf( libname, 1024, "%s/%s.so", prefixes[ pindex ], arg );
		if( stat( libname, &junk ) == 0 ) {
			break;
		}
	}

	/* open lib */
	lib = dlopen( libname, RTLD_GLOBAL | RTLD_LAZY );
	if( ! lib ) {
		PRINTF( "#module error: %s\n", dlerror() );
		return;
	}else{
		PRINTF( "#module loaded %s\n", libname );
	}

	func = dlsym( lib, "powwow_init" );
	if( func ) {
		(*func)();
	}else{
		PRINTF( "#module error: %s\n", dlerror() );
	}
}
#endif

static void cmd_group __P1 (char *,arg) {
    char *group;
    int active;
    aliasnode *p;
    actionnode *a;

    arg = skipspace(arg);

    if( *arg ) {
    	arg = first_regular( group = arg, ' ');
	*arg = '\0';
	arg = skipspace( arg + 1 );

	if( strcmp( arg, "on" ) == 0 ) {
		active = 1;
	}else if( strcmp( arg, "off" ) == 0 ) {
		active = 0;
	}else if( strcmp( arg, "list" ) == 0 ) {
		PRINTF( "#not implemented\n" );
		return;
	}else{
		PRINTF( "#unknown group command, use off/on/list\n" );
		return;
	}

	/* Now loop over all aliases/actions by groupname and toggle */
	for( p = sortedaliases; p; p = p -> snext ) {
		if( p -> group && strcmp( p -> group, group ) == 0 ) {
			p -> active = active;
		}
	}

	/* Same for actions */
	for( a = actions; a; a = a -> next ) {
		if( a -> group && strcmp( a -> group, group ) == 0 ) {
			a -> active = active;
		}
	}
    }else{
	PRINTF( "#group name required\n" );
    }
}

static void cmd_groupdelim __P1 (char *,arg) {
    if( *arg != 0 ) {
        free( group_delim );
        group_delim = my_strdup( arg );
	    PRINTF( "#group delimiter is now '%s'\n", group_delim );
    }
}

static void cmd_help __P1 (char *,arg)
{
    int i, size;
    char *text, *tmp;
    FILE *f;
    char line[BUFSIZE];
    int len;
    cmdstruct *c;
    
    arg = skipspace(arg);
    if (*arg == '#') arg++;
    if (!*arg) {
	size = 25;
	for( c = commands; c != NULL; c = c -> next )
	    size += strlen(c -> name) + strlen(c -> help) + 5;
	
	text = tmp = (char *)malloc(size);
	if (!text) {
	    errmsg("malloc");
	    return;
	}

	/* do not use sprintf() return value, almost every OS returns a different thing. */
        sprintf(tmp, "#help\n#commands available:\n");
	tmp += strlen(tmp);

	for( c = commands; c != NULL; c = c -> next ) {
	    sprintf(tmp, "#%s %s\n", c -> name, c -> help);
	    tmp += strlen(tmp);
	}
	
	message_edit(text, strlen(text), 1, 1);
	return;
    }
	
    if (!strncmp(arg, "copyright", strlen(arg))) {
	int fd, left, got = 0;
	struct stat stbuf;
	    
	if (stat(copyfile, &stbuf) < 0) {
	    errmsg("stat(copyright file)");
	    return;
	}
		
	if (!(text = (char *)malloc(left = stbuf.st_size))) {
	    errmsg("malloc");
	    return;
	}
	if ((fd = open(copyfile, O_RDONLY)) < 0) {
	    errmsg("open(copyright file)");
	    free(text);
	    return;
	}
	while (left > 0) {
	    while ((i = read(fd, text + got, left)) < 0 && errno == EINTR)
	      ;
	    if (i < 0 && errno == EINTR) {
		errmsg("read (copyright file)");
		free(text);
		close(fd);
		return;
	    }
	    if (i == 0)
		break;
	    left -= i, got += i;
	}
	close(fd);
	message_edit(text, strlen(text), 1, 1);
	return;
    }
    
    /* !copyright */
    
    f = fopen(helpfile, "r");
    if (!f) {
	PRINTF("#cannot open help file \"%s\": %s\n",
	       helpfile, strerror(errno));
	return;
    }
    
    while ((tmp = fgets(line, BUFSIZE, f)) &&
	   (line[0] != '@' || strncmp(line + 1, arg, strlen(arg))))
	;
	    
    if (!tmp) {
	PRINTF("#no entry for \"%s\" in the help file.\n", arg);
	fclose(f);
	return;
    }

    if (!(text = (char *)malloc(size = BUFSIZE))) {
	errmsg("malloc");
	fclose(f);
	return;
    }

    /* the first line becomes $TITLE */
    tmp = strchr(line, '\n');
    if (tmp) *tmp = '\0';
    i = sprintf(text, "Help on '%s'\n", line + 1);

    /* allow multiple commands to share the same help */
    while (fgets(line, BUFSIZE, f) && line[0] == '@') ;

    do {
	if ((len = strlen(line)) >= size - i) {
	    /* Not enough space in current buffer */
	  
	    if (!(tmp = (char *)malloc(size += BUFSIZE))) {
		errmsg("malloc");
		free(text);
		fclose(f);
		return;
	    } else {
		memcpy(tmp, text, i);
		free(text);
		text = tmp;
	    }
	}
	memcpy(text + i, line, len);
	i += len;
    } while (fgets(line, BUFSIZE, f) && line[0] != '@');
    
    fclose(f);
    text[i] = '\0'; /* safe, there is space */
    message_edit(text, strlen(text), 1, 1);
}

static void cmd_clear __P1 (char *,arg)
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

#ifndef NO_SHELL
static void cmd_shell __P1 (char *,arg)
{
    if (!*arg) {
        if (opt_info) {
	    PRINTF("#that's easy.\n");
        }
    } else {
        tty_quit();

        if (system(arg) == -1) {
            perror("system()");
        }

        tty_start();
        tty_gotoxy(col0 = 0, line0 = lines -1);
        tty_puts(tty_clreoln);
    }
}
#endif

static void cmd_alias __P1 (char *,arg)
{
    arg = skipspace(arg);
    if (!*arg)
	show_aliases();
    else
	parse_alias(arg);
}

static void cmd_action __P1 (char *,arg)
{
    arg = skipspace(arg);
    if (!*arg)
	show_actions();
    else
	parse_action(arg, 0);
}

static void cmd_prompt __P1 (char *,arg)
{
    arg = skipspace(arg);
    if (!*arg)
	show_prompts();
    else
	parse_action(arg, 1);
}

static void cmd_beep __P1 (char *,arg)
{
    tty_putc('\007');
}

/*
 * create/list/edit/delete bindings
 */
static void cmd_bind __P1 (char *,arg)
{
    arg = skipspace(arg);
    if (!*arg)
	show_binds(0);
    else if (!strcmp(arg, "edit"))
	show_binds(1);
    else
	parse_bind(arg);
}

static void cmd_delim __P1 (char *,arg)
{
    char buf[BUFSIZE];
    int n;

    arg = skipspace(arg);
    if (!*arg) {
	PRINTF("#delim: \"%s\" (%s)\n", delim_name[delim_mode], DELIM);
	return;
    }

    arg = split_first_word(buf, BUFSIZE, arg);
    n = 0;
    while (n < DELIM_MODES && strncmp(delim_name[n], buf, strlen(buf)) != 0)
	n++;
    
    if (n >= DELIM_MODES) {
	PRINTF("#delim [normal|program|{custom <chars>}\n");
	return;
    }

    if (n == DELIM_CUSTOM) {
	if (!strchr(arg, ' ')) {
	    my_strncpy(buf+1, arg, BUFSIZE-2);
	    *buf = ' ';				/* force ' ' in the delims */
	    arg = buf;
	}
	unescape(arg);
	set_custom_delimeters(arg);
    } else
	delim_mode = n;
}

static void cmd_do __P1 (char *,arg)
{
    int type;
    long result;
    
    arg = skipspace(arg);
    if (*arg != '(') {
	PRINTF("#do: ");
	print_error(error=MISMATCH_PAREN_ERROR);
	return;
    }
    arg++;
    
    type = evall(&result, &arg);
    if (REAL_ERROR) return;
    
    if (type != TYPE_NUM) {
	PRINTF("#do: ");
	print_error(error=NO_NUM_VALUE_ERROR);
	return;
    }
    
    if (*arg == ')') {          /* skip the ')' */
	if (*++arg == ' ')
	    arg++;
    }
    else {
	PRINTF("#do: ");
	print_error(error=MISSING_PAREN_ERROR);
	return;
    }
    
    if (result >= 0)
	while (!error && result--)
	    (void)parse_instruction(arg, 1, 0, 1);
    else {
	PRINTF("#do: bogus repeat count \"%ld\"\n", result);
    }
}

static void cmd_hilite __P1 (char *,arg)
{
    int attr;
    
    arg = skipspace(arg);
    attr = parse_attributes(arg);
    if (attr == -1) {
	PRINTF("#attribute syntax error.\n");
	if (opt_info)
	  show_attr_syntax();
    } else {
	attr_string(attr, edattrbeg, edattrend);

	edattrbg = ATTR(attr) & ATTR_INVERSE ? 1
	    : BACKGROUND(attr) != NO_COLOR || ATTR(attr) & ATTR_BLINK;
	
	if (opt_info) {
	    PRINTF("#input highlighting is now %so%s%s.\n",
		       edattrbeg, (attr == NOATTRCODE) ? "ff" : "n",
		       edattrend);
	}
    }
}

static void cmd_history __P1 (char *,arg)
{
    int num = 0;
    long buf;
    
    arg = skipspace(arg);
    
    if (history_done >= MAX_HIST) {
	print_error(error=HISTORY_RECURSION_ERROR);
	return;
    }
    history_done++;
    
    if (*arg == '(') {
	arg++;
	num = evall(&buf, &arg);
	if (!REAL_ERROR && num != TYPE_NUM)
	    error=NO_NUM_VALUE_ERROR;
	if (REAL_ERROR) {
	    PRINTF("#history: ");
	    print_error(error=NO_NUM_VALUE_ERROR);
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

static void cmd_host __P1 (char *,arg)
{
    char newhost[BUFSIZE];
    
    arg = skipspace(arg);
    if (*arg) {
	arg = split_first_word(newhost, BUFSIZE, arg);
	if (*arg) {
	    my_strncpy(hostname, newhost, BUFSIZE-1);
	    portnumber = atoi(arg);
	    if (opt_info) {
		PRINTF("#host set to: %s %d\n", hostname, portnumber);
	    }
	} else {
	    PRINTF("#host: missing portnumber.\n");
	}
    } else if (*hostname)
	sprintf(inserted_next, "#host %.*s %d", BUFSIZE-INTLEN-8,
		hostname, portnumber);
    else {
	PRINTF("#syntax: #host hostname port\n");
    }
}

static void cmd_request __P1 (char *,arg)
{
    char *idprompt = "~$#EP2\nG\n";
    char *ideditor = "~$#EI\n";
    char buf[256];
    int all, len;
    
    if (tcp_fd == -1) {
	PRINTF("#not connected to a MUD!\n");
	return;
    }
    while (*(arg = skipspace(arg))) {
	arg = split_first_word(buf, 256, arg);
	if (*buf) {
	    all = !strcmp(buf, "all");
	    len = strlen(buf);
	    if ((all || !strncmp(buf, "editor", len))) {
		tcp_raw_write(tcp_fd, ideditor, strlen(ideditor));
		CONN_LIST(tcp_fd).flags |= IDEDITOR;
		if (opt_info) {
		    PRINTF("#request editor: %s done!\n", ideditor);
		}
	    }
	    if ((all || !strncmp(buf, "prompt", len))) {
		tcp_raw_write(tcp_fd, idprompt, strlen(idprompt));
		CONN_LIST(tcp_fd).flags |= IDPROMPT;
		if (opt_info) {
		    PRINTF("#request prompt: %s done!\n", idprompt);
		}
	    }
	}
    }
    
}

static void cmd_identify __P1 (char *,arg)
{
    edit_start[0] = edit_end[0] = '\0';
    if (*arg) {
        char *p = strchr(arg, ' ');
        if (p) {
            *(p++) = '\0';
            my_strncpy(edit_end, p, BUFSIZE-1);
        }
        my_strncpy(edit_start, arg, BUFSIZE-1);
    }
    cmd_request("editor");
}

static void cmd_in __P1 (char *,arg)
{
    char *name;
    long millisec, buf;
    int type;
    delaynode **p;
    
    arg = skipspace(arg);
    if (!*arg) {
	show_delays();
	return;
    }
    
    arg = first_regular(name = arg, ' ');
    if (*arg)
	*arg++ = 0;
    
    unescape(name);
    
    p = lookup_delay(name, 0);
    if (!*p)  p = lookup_delay(name, 1);
    
    if (!*arg && !*p) {
	PRINTF("#unknown delay label, cannot show: \"%s\"\n", name);
	return;
    }
    if (!*arg) {
	show_delaynode(*p, 1);
	return;
    }
    if (*arg != '(') {
	PRINTF("#in: ");
	print_error(error=MISMATCH_PAREN_ERROR);
	return;
    }
    arg++;    /* skip the '(' */
    
    type = evall(&buf, &arg);
    if (!REAL_ERROR) {
	if (type!=TYPE_NUM)
	    error=NO_NUM_VALUE_ERROR;
	else if (*arg != ')')
	    error=MISSING_PAREN_ERROR;
    }
    if (REAL_ERROR) {
	PRINTF("#in: ");
	print_error(error);
	return;
    }
    
    arg = skipspace(arg+1);
    millisec = buf;
    if (*p && millisec)
	change_delaynode(p, arg, millisec);
    else if (!*p && millisec) {
	if (*arg)
	    new_delaynode(name, arg, millisec);
	else {
	    PRINTF("#cannot create delay label without a command.\n");
	}
    } else if (*p && !millisec) {
	if (opt_info) {
	    PRINTF("#deleting delay label: %s %s\n", name, (*p)->command);
	}
	delete_delaynode(p);
    } else {
	PRINTF("#unknown delay label, cannot delete: \"%s\"\n", name);
    }
}

static void cmd_at __P1 (char *,arg)
{
    char *name, *buf = NULL;
    char dayflag=0;
    struct tm *twhen;
    int num, hour = -1, minute = -1, second = -1;
    delaynode **p;
    long millisec;
    ptr pbuf = (ptr)0;
    
    arg = skipspace(arg);
    if (!*arg) {
	show_delays();
	return;
    }
    
    arg = first_regular(name = arg, ' ');
    if (*arg)
	*arg++ = 0;
    
    unescape(name);
    
    p = lookup_delay(name, 0);
    if (!*p)  p = lookup_delay(name, 1);
    
    if (!*arg && !*p) {
	PRINTF("#unknown delay label, cannot show: \"%s\"\n", name);
	return;
    }
    if (!*arg) {
	show_delaynode(*p, 2);
	return;
    }
    if (*arg != '(') {
	PRINTF("#in: ");
	print_error(error=MISMATCH_PAREN_ERROR);
	return;
    }
    arg++;    /* skip the '(' */
    
    (void)evalp(&pbuf, &arg);
    if (REAL_ERROR) {
	print_error(error);
	ptrdel(pbuf);
	return;
    }
    if (pbuf) {
	/* convert time-string into hour, minute, second */
	buf = skipspace(ptrdata(pbuf));
	if (!*buf || !isdigit(*buf)) {
	    PRINTF("#at: ");
	    print_error(error=NO_NUM_VALUE_ERROR);
	    ptrdel(pbuf);
	    return;
	}
	num = atoi(buf);
	second = num % 100;
	minute = (num /= 100) % 100;
	hour   = num / 100;
    }
    if (hour < 0 || hour>23 || minute < 0 || minute>59
	|| second < 0 || second>59) {
	
	PRINTF("#at: #error: invalid time \"%s\"\n",
	       pbuf && buf ? buf : (char *)"");
	error=OUT_RANGE_ERROR;
	ptrdel(pbuf);
	return;
    }
    ptrdel(pbuf);

    if (*arg == ')') {        /* skip the ')' */
	if (*++arg == ' ')
	    arg++;
    }
    else {
	PRINTF("#at: ");
	print_error(error=MISSING_PAREN_ERROR);
	return;
    }
    
    arg = skipspace(arg);
    update_now();
    twhen = localtime((time_t *)&now.tv_sec); 
    /* put current year, month, day in calendar struct */

    if (hour < twhen->tm_hour || 
	(hour == twhen->tm_hour && 
	 (minute < twhen->tm_min || 
	  (minute == twhen->tm_min &&
	   second <= twhen->tm_sec)))) {
	dayflag = 1;
        /* it is NOT possible to define an #at refering to the past */
    }

    /* if you use a time smaller than the current, it refers to tomorrow */
    
    millisec = (hour - twhen->tm_hour) * 3600 + (minute - twhen->tm_min) * 60 + 
	second - twhen->tm_sec + (dayflag ? 24*60*60 : 0);
    millisec *= mSEC_PER_SEC; /* Comparing time with current calendar,
			       we finally got the delay */
    millisec -= now.tv_usec / uSEC_PER_mSEC;
    
    if (*p)
	change_delaynode(p, arg, millisec);
    else
	if (*arg)
	new_delaynode(name, arg, millisec);
    else {
	PRINTF("#cannot create delay label without a command.\n");
    }
}

static void cmd_init __P1 (char *,arg)
{
    arg = skipspace(arg);
    
    if (*arg == '=') {
	if (*++arg) {
	    my_strncpy(initstr, arg, BUFSIZE-1);
	    if (opt_info) {
		PRINTF("#init: %s\n", initstr);
	    }
	} else {
	    *initstr = '\0';
	    if (opt_info) {
		PRINTF("#init cleared.\n");
	    }
	}
    } else
	sprintf(inserted_next, "#init =%.*s", BUFSIZE-8, initstr);
}

static void cmd_isprompt __P1 (char *,arg)
{
    if (tcp_fd == tcp_main_fd) {
	int i;
	long l;
	arg = skipspace(arg);
	if (*arg == '(') {
	    arg++;
	    i = evall(&l, &arg);
	    if (!REAL_ERROR) {
		if (i!=TYPE_NUM)
		  error=NO_NUM_VALUE_ERROR;
		else if (*arg != ')')
		  error=MISSING_PAREN_ERROR;
	    }
	    if (REAL_ERROR) {
		PRINTF("#isprompt: ");
		print_error(error);
		return;
	    }
	    i = (int)l;
	} else
	    i = atoi(arg);
	
	if (i == 0)
	    surely_isprompt = -1;
	else if (i < 0) {
	    if (i > -NUMPARAM && *VAR[-i].str)
		ptrtrunc(prompt->str, surely_isprompt = ptrlen(*VAR[-i].str));
	} else
	    ptrtrunc(prompt->str, surely_isprompt = i);
    }
}

static void cmd_key __P1 (char *,arg)
{
    keynode *q=NULL;
    
    arg = skipspace(arg);
    if (!*arg)
	return;
    
    if ((q = *lookup_key(arg)))
	q->funct(q->call_data);
    else {
	PRINTF("#no such key: \"%s\"\n", arg);
    }
}

static void cmd_keyedit __P1 (char *,arg)
{
    int function;
    char *param;
    
    arg = skipspace(arg);
    if (!*arg)
	return;
    
    if ((function = lookup_edit_name(arg, &param)))
	internal_functions[function].funct(param);
    else {
	PRINTF("#no such editing function: \"%s\"\n", arg);
    }
}

static void cmd_map __P1 (char *,arg)
{
    arg = skipspace(arg);
    if (!*arg)  /* show map */
	map_show();
    else if (*arg == '-')  /* retrace steps without walking */
	map_retrace(atoi(arg + 1), 0);
    else
	map_walk(arg, 1, 1);
}

static void cmd_retrace __P1 (char *,arg)
{
    map_retrace(atoi(arg), 1);
}

static void cmd_mark __P1 (char *,arg)
{
    if (!*arg)
	show_marks();
    else
	parse_mark(arg);
}

static void cmd_nice __P1 (char *,arg)
{
    int nnice = a_nice;
    arg = skipspace(arg);
    if (!*arg) {
	PRINTF("#nice: %d\n", a_nice);
	return;
    }
    if (isdigit(*arg)) {
	a_nice = 0;
	while (isdigit(*arg)) {
	    a_nice *= 10;
	    a_nice += *arg++ - '0';
	}
    }
    else if (*arg++=='(') {
	long buf;
	int type;

	type = evall(&buf, &arg);
	if (!REAL_ERROR && type!=TYPE_NUM)
	    error=NO_NUM_VALUE_ERROR;
	else if (!REAL_ERROR && *arg++ != ')')
	    error=MISSING_PAREN_ERROR;
	if (REAL_ERROR) {
	    PRINTF("#nice: ");
	    print_error(error);
	    return;
	}
	a_nice = (int)buf;
	if (a_nice<0)
	    a_nice = 0;
    }
    arg = skipspace(arg);
    if (*arg) {
	parse_instruction(arg, 0, 0, 1);
	a_nice = nnice;
    }
}

static void cmd_prefix __P1 (char *,arg)
{
    strcpy(prefixstr, arg);
    if (opt_info) {
	PRINTF("#prefix %s.\n", *arg ? "set" : "cleared");
    }
}

static void cmd_quote __P1 (char *,arg)
{
    arg = skipspace(arg);
    if (!*arg)
	verbatim ^= 1;
    else if (!strcmp(arg, "on"))
	verbatim = 1;
    else if (!strcmp(arg, "off"))
	verbatim = 0;
    if (opt_info) {
        PRINTF("#%s mode.\n", verbatim ? "verbatim" : "normal");
    }
}

/*
 * change the escape sequence of an existing binding
 */
static void cmd_rebind __P1 (char *,arg)
{
    parse_rebind(arg);
}

static void cmd_rebindall __P1 (char *,arg)
{
    keynode *kp;
    char *seq;
    
    for (kp = keydefs; kp; kp = kp->next) {
	seq = kp->sequence;
	if (kp->seqlen == 1 && seq[0] < ' ')
	    ;
	else if (kp->seqlen == 2 && seq[0] == '\033' && isalnum(seq[1]))
	    ;
	else {
	    parse_rebind(kp->name);
	    if (error)
		break;
	}
    }
}

static void cmd_rebindALL __P1 (char *,arg)
{
    keynode *kp;
    
    for (kp = keydefs; kp; kp = kp->next) {
	parse_rebind(kp->name);
	if (error)
	    break;
    }
}

static void cmd_reset __P1 (char *,arg)
{
    char all = 0;
    arg = skipspace(arg);
    if (!*arg) {
        PRINTF("#reset: must specify one of:\n");
        tty_puts(" alias, action, bind, in (or at), mark, prompt, var, all.\n");
	return;
    }
    all = !strcmp(arg, "all");
    if (all || !strcmp(arg, "alias")) {
        int n;
	for (n = 0; n < MAX_HASH; n++) {
	    while (aliases[n])
		delete_aliasnode(&aliases[n]);
	}
	if (!all)
	    return;
    }
    if (all || !strcmp(arg, "action")) {
        while (actions)
	    delete_actionnode(&actions);
	if (!all)
	    return;
    }
    if (all || !strcmp(arg, "bind")) {
        while (keydefs)
	    delete_keynode(&keydefs);
	tty_add_initial_binds();
	tty_add_walk_binds();
	if (!all)
	    return;
    }
    if (all || !strcmp(arg, "in") || !strcmp(arg, "at")) {
        while (delays)
	    delete_delaynode(&delays);
        while (dead_delays)
	    delete_delaynode(&dead_delays);
	if (!all)
	    return;
    }
    if (all || !strcmp(arg, "mark")) {
        while (markers)
	    delete_marknode(&markers);
	if (!all)
	    return;
    }
    if (all || !strcmp(arg, "prompt")) {
        while (prompts)
	    delete_promptnode(&prompts);
	if (!all)
	    return;
    }
    if (all || !strcmp(arg, "var")) {
        int n;
	varnode **first;

	for (n = 0; n < MAX_HASH; n++) {
	    while (named_vars[0][n])
		delete_varnode(&named_vars[0][n], 0);
	    first = &named_vars[1][n];
	    while (*first) {
		if (is_permanent_variable(*first))
		    first = &(*first)->next;
		else	
		    delete_varnode(first, 1);
	    }
	}
	
	for (n = 0; n < NUMVAR; n++) {
	    *var[n].num = 0;
	    ptrdel(*var[n].str);
	    *var[n].str = NULL;
	}
	if (!all)
	    return;
    }
}

static void cmd_snoop __P1 (char *,arg)
{
    if (!*arg) {
	PRINTF("#snoop: which connection?\n");
    } else
	tcp_togglesnoop(arg);
}

static void cmd_stop __P1 (char *,arg)
{
    delaynode *dying;
    
    if (delays)
	update_now();
    
    while (delays) {
	dying = delays;
	delays = dying->next;
	dying->when.tv_sec = now.tv_sec;
	dying->when.tv_usec = now.tv_usec;
	dying->next = dead_delays;
	dead_delays = dying;
    }
    if (opt_info) {
	PRINTF("#all delayed labels are now disabled.\n");
    }
}

static void cmd_time __P1 (char *,arg)
{
    struct tm *s;
    char buf[BUFSIZE];
    
    update_now();
    s = localtime((time_t *)&now.tv_sec);
    (void)strftime(buf, BUFSIZE - 1, "%a,  %d %b %Y  %H:%M:%S", s);
    PRINTF("#current time is %s\n", buf);
}

static void cmd_ver __P1 (char *,arg)
{
    printver();
}

static void cmd_emulate __P1 (char *,arg)
{
    char kind;
    FILE *fp;
    long start, end, i = 1;
    int len;
    ptr pbuf = (ptr)0;
    
    arg = redirect(arg, &pbuf, &kind, "emulate", 0, &start, &end);
    if (REAL_ERROR || !arg)
	return;
    
    if (kind) {
	char buf[BUFSIZE];
	
	fp = (kind == '!') ? popen(arg, "r") : fopen(arg, "r");
	if (!fp) {
	    PRINTF("#emulate: #error opening \"%s\"\n", arg);
	    print_error(error=SYNTAX_ERROR);
	    ptrdel(pbuf);
	    return;
	}
	status(-1); /* we're pretending we got something from the MUD */
	while (!error && (!start || i<=end) && fgets(buf, BUFSIZE, fp))
	    if (!start || i++>=start)
		process_remote_input(buf, strlen(buf));
	
	if (kind == '!') pclose(fp); else fclose(fp);
    } else {
	status(-1); /* idem */
	/* WARNING: no guarantee there is space! */
	arg[len = strlen(arg)] = '\n';
	arg[++len] = '\0';
	process_remote_input(arg, len);
    }
    ptrdel(pbuf);
}

static void cmd_eval __P1 (char *,arg)
{
    arg = skipspace(arg);
    if (*arg=='(') {
	arg++;
	(void)evaln(&arg);
	if (*arg != ')') {
	    PRINTF("#(): ");
	    print_error(error=MISSING_PAREN_ERROR);
	}
    }
    else {
	PRINTF("#(): ");
	print_error(error=MISMATCH_PAREN_ERROR);
    }
}

static void cmd_exe __P1 (char *,arg)
{
    char kind;
    char *clear;
    long offset, start, end, i = 1;
    FILE *fp;
    ptr pbuf = (ptr)0;
    
    arg = redirect(arg, &pbuf, &kind, "exe", 0, &start, &end);
    if (REAL_ERROR || !arg)
	return;

    if (kind) {
	char buf[BUFSIZE];
	
	fp = (kind == '!') ? popen(arg, "r") : fopen(arg, "r");
	if (!fp) {
	    PRINTF("#exe: #error opening \"%s\"\n", arg);
	    error = SYNTAX_ERROR;
	    ptrdel(pbuf);
	    return;
	}
	offset = 0;
	/* We may go in to a loop if a single function is more than 4k, but if that's
	 * the case then maybe you should break it down a little bit :p */
	while (!error && (!start || i<=end) && fgets(buf + offset, BUFSIZE - offset, fp))
	    /* If it ends with \\\n then it's a line continuation, so clear
	     * the \\\n and do another fgets */
	    if (buf[offset + strlen(buf + offset) - 2] == '\\') {
		/* Clear \n prefixed with a literal backslash '\\' */
		if ((clear = strstr(buf + offset, "\\\n")))
		    *clear = '\0';
		offset += strlen(buf + offset);
	    } else {
	        if (!start || i++ >= start) {
		    buf[strlen(buf)-1] = '\0';
		    parse_user_input(buf, 0);
		    offset = 0;
		}
	    }
	
	if (kind == '!') pclose(fp); else fclose(fp);
    } else
	parse_user_input(arg, 0);
    ptrdel(pbuf);
}

static void cmd_print __P1 (char *,arg)
{
    char kind;
    long start, end, i = 1;
    FILE *fp;
    ptr pbuf = (ptr)0;
    
    clear_input_line(opt_compact);
    
    if (!*arg) {
	smart_print(*VAR[0].str ? ptrdata(*VAR[0].str) : (char *)"", 1);
	return;
    }
    
    arg = redirect(arg, &pbuf, &kind, "print", 1, &start, &end);
    if (REAL_ERROR || !arg)
	return;

    if (kind) {
	char buf[BUFSIZE];
	fp = (kind == '!') ? popen(arg, "r") : fopen(arg, "r");
	if (!fp) {
	    PRINTF("#print: #error opening \"%s\"\n", arg);
	    error=SYNTAX_ERROR;
	    ptrdel(pbuf);
	    return;
	}
	while (!error && (!start || i <= end) && fgets(buf, BUFSIZE, fp))
	    if (!start || i++>=start)
		tty_puts(buf);
	tty_putc('\n');
	
	if (kind == '!') pclose(fp); else fclose(fp);
    } else
	smart_print(arg, 1);
    ptrdel(pbuf);
}

static void cmd_send __P1 (char *,arg)
{
    char *newline, kind;
    long start, end, i = 1;
    FILE *fp;
    ptr pbuf = (ptr)0;
    
    arg = redirect(arg, &pbuf, &kind, "send", 0, &start, &end);
    if (REAL_ERROR ||!arg)
	return;
    
    if (kind) {
	char buf[BUFSIZE];
	fp = (kind == '!') ? popen(arg, "r") : fopen(arg, "r");
	if (!fp) {
	    PRINTF("#send: #error opening \"%s\"\n", arg);
	    error = SYNTAX_ERROR;
	    ptrdel(pbuf);
	    return;
	}
	while (!error && (!start || i<=end) && fgets(buf, BUFSIZE, fp)) {
	    if ((newline = strchr(buf, '\n')))
		*newline = '\0';
	    
	    if (!start || i++>=start) {
		if (opt_echo) {
		    PRINTF("[%s]\n", buf);
		}
		tcp_write(tcp_fd, buf);
	    }
	}
	if (kind == '!') pclose(fp); else fclose(fp);
    } else {
	if (opt_echo) {
	    PRINTF("[%s]\n", arg);
	}
	tcp_write(tcp_fd, arg);
    }
    ptrdel(pbuf);
}

static void cmd_rawsend __P1 (char *,arg)
{
    char *tmp = skipspace(arg);
    
    if (*tmp=='(') {
	ptr pbuf = (ptr)0;
	arg = tmp + 1;
	(void)evalp(&pbuf, &arg);
	if (REAL_ERROR) {
	    print_error(error);
	    ptrdel(pbuf);
	} else if (pbuf)
	    tcp_raw_write(tcp_fd, ptrdata(pbuf), ptrlen(pbuf));
    } else {
	int len;
	if ((len = memunescape(arg, strlen(arg))))
	    tcp_raw_write(tcp_fd, arg, len);
    }
}

static void cmd_rawprint __P1 (char *,arg)
{
    char *tmp = skipspace(arg);
    
    if (*tmp=='(') {
	ptr pbuf = (ptr)0;
	arg = tmp + 1;
	(void)evalp(&pbuf, &arg);
	if (REAL_ERROR) {
	    print_error(error);
	    ptrdel(pbuf);
	} else if (pbuf)
	    tty_raw_write(ptrdata(pbuf), ptrlen(pbuf));
    } else {
	int len;
	if ((len = memunescape(arg, strlen(arg))))
	    tty_raw_write(arg, len);
    }
}


static void cmd_write __P1 (char *,arg)
{
    ptr p1 = (ptr)0, p2 = (ptr)0;
    char *tmp = skipspace(arg), kind;
    FILE *fp;
    
    kind = *tmp;
    if (kind == '!' || kind == '>')
	arg = ++tmp;
    else
	kind = 0;
    
    if (*tmp=='(') {
	arg = tmp + 1;
	(void)evalp(&p1, &arg);
	if (REAL_ERROR)
	    goto write_cleanup;
	
	if (*arg == CMDSEP) {
	    arg++;
	    (void)evalp(&p2, &arg);
	    if (!REAL_ERROR && !p2)
		error = NO_STRING_ERROR;
	    if (REAL_ERROR)
		goto write_cleanup;
	} else {
	    PRINTF("#write: ");
	    error=SYNTAX_ERROR;
	    goto write_cleanup;
	}
	if (*arg != ')') {
	    PRINTF("#write: ");
	    error=MISSING_PAREN_ERROR;
	    goto write_cleanup;
	}
	arg = ptrdata(p2);
	
	fp = (kind == '!') ? popen(arg, "w") : fopen(arg, kind ? "w" : "a");
	if (!fp) {
	    PRINTF("#write: #error opening \"%s\"\n", arg);
	    error=SYNTAX_ERROR;
	    goto write_cleanup2;
	}
	fprintf(fp, "%s\n", p1 ? ptrdata(p1) : (char *)"");
	fflush(fp);
	if (kind == '!') pclose(fp); else fclose(fp);
    } else {
	PRINTF("#write: ");
	error=MISMATCH_PAREN_ERROR;
    }

write_cleanup:
    if (REAL_ERROR)
	print_error(error);
write_cleanup2:
    ptrdel(p1);
    ptrdel(p2);
}

static void cmd_var __P1 (char *,arg)
{
    char *buf, *expr, *tmp, kind, type, right = 0, deleting = 0;
    varnode **p_named_var = NULL, *named_var = NULL;
    FILE *fp;
    long start, end, i = 1;
    int len, idx;
    ptr pbuf = (ptr)0;
    
    arg = skipspace(arg);
    expr = first_regular(arg, '=');
    
    if (*expr) {
	*expr++ = '\0';     /* skip the = */
	if (!*expr)
	    deleting = 1;
	else {
	    right = 1;
	    if (*expr == ' ')
		expr++;
	}
    }
    
    if (*arg == '$')
	type = TYPE_TXT_VAR;
    else if (*arg == '@')
	type = TYPE_NUM_VAR;
    else if (*arg) {
	print_error(error=INVALID_NAME_ERROR);
	return;
    } else {
	show_vars();
	return;
    }
    
    kind = *++arg;
    if (isalpha(kind) || kind == '_') {
	/* found a named variable */
	tmp = arg;
	while (*tmp && (isalnum(*tmp) || *tmp == '_'))
	    tmp++;
	if (*tmp) {
	    print_error(error=INVALID_NAME_ERROR);
	    return;
	}
	kind = type==TYPE_TXT_VAR ? 1 : 0;
	p_named_var = lookup_varnode(arg, kind);
	if (!*p_named_var) {
	    /* it doesn't (yet) exist */
	    if (!deleting) {
		/* so create it */
		named_var = add_varnode(arg, kind);
		if (REAL_ERROR)
		    return;
		if (opt_info) {
		    PRINTF("#new variable: \"%s\"\n", arg - 1);
		}
	    } else {
		print_error(error=UNDEFINED_VARIABLE_ERROR);
		return;
	    }
	} else
	    /* it exists, hold on */
	    named_var = *p_named_var;
	
	idx = named_var->index;
    } else {
	/* not a named variable, may be
	 * an unnamed variable or an expression */
	kind = type==TYPE_TXT_VAR ? 1 : 0;
	tmp = skipspace(arg);
	if (*tmp == '(') {
	    /* an expression */
	    arg = tmp+1;
	    idx = evalp(&pbuf, &arg);
	    if (!REAL_ERROR && idx != TYPE_TXT)
		error=NO_STRING_ERROR;
	    if (REAL_ERROR) {
		PRINTF("#var: ");
		print_error(error);
		ptrdel(pbuf);
		return;
	    }
	    if (pbuf)
		buf = ptrdata(pbuf);
	    else {
		print_error(error=INVALID_NAME_ERROR);
		return;
	    }
	    char err_det;
	    if (isdigit(*buf) || *buf=='-' || *buf=='+') {
		if (sscanf(buf, "%d%c", &idx, &err_det)==1) {
		    if (idx < -NUMVAR || idx >= NUMPARAM) {
			print_error(error=OUT_RANGE_ERROR);
			ptrdel(pbuf);
			return;
		    }
		} else {
		    print_error(error=INVALID_NAME_ERROR);
		    return;		
		}
	    } else {
		if (!isalpha(*buf) && *buf!='_') {
		    print_error(error=INVALID_NAME_ERROR);
		    return;
		}
		tmp = buf + 1;
	        while (*tmp && (isalnum(*tmp) || *tmp=='_'))
		    tmp++;		
		if (*tmp) {
		    print_error(error=INVALID_NAME_ERROR);
		    return;
		}
		if (!(named_var = *(p_named_var = lookup_varnode(buf, kind)))) {
		    if (!deleting) {
			named_var = add_varnode(buf, kind);
			if (REAL_ERROR) {
			    print_error(error);
			    ptrdel(pbuf);
			    return;
			}
			if (opt_info) {
			    PRINTF("#new variable: %c%s\n", kind
				       ? '$' : '@', buf);
			}
		    } else {
			print_error(error=UNDEFINED_VARIABLE_ERROR);
			ptrdel(pbuf);
			return;
		    }
		}
		idx = named_var->index;
	    }
	} else {
	    /* an unnamed var */
	    long buf2;
	    
	    idx = evall(&buf2, &arg);
	    if (!REAL_ERROR && idx != TYPE_NUM)
		error=NO_STRING_ERROR;
	    if (REAL_ERROR) {
		PRINTF("#var: ");
		print_error(error);
		return;
	    }
	    idx = (int)buf2;
	    if (idx < -NUMVAR || idx >= NUMPARAM) {
		print_error(error=OUT_RANGE_ERROR);
		return;
	    }
	    /* ok, it's an unnamed var */
	}
    }

    
    if (type == TYPE_TXT_VAR && right && !*VAR[idx].str) {
	/* create it */
	*VAR[idx].str = ptrnew(PARAMLEN);
	if (MEM_ERROR) {
	    print_error(error);
	    ptrdel(pbuf);
	    return;
	}
    }

    if (deleting) {
	/* R.I.P. named variables */
	if (named_var) {
	    if (is_permanent_variable(named_var)) {
		PRINTF("#cannot delete variable: \"%s\"\n", arg - 1);
	    } else {
		delete_varnode(p_named_var, kind);
		if (opt_info) {
		    PRINTF("#deleted variable: \"%s\"\n", arg - 1);
		}
	    }
	} else if ((type = TYPE_TXT_VAR)) {
	/* R.I.P. unnamed variables */	    
	    if (*VAR[idx].str) {
		if (idx < 0) {
		    ptrdel(*VAR[idx].str);
		    *VAR[idx].str = 0;
		} else
		    ptrzero(*VAR[idx].str);
	    }
	} else
	    *VAR[idx].num = 0;
	ptrdel(pbuf);
	return;
    } else if (!right) {
	/* no right-hand expression, just show */
	if (named_var) {
	    if (type == TYPE_TXT_VAR) {
		pbuf = ptrescape(pbuf, *VAR[idx].str, 0);
		if (REAL_ERROR) {
		    print_error(error);
		    ptrdel(pbuf);
		    return;
		}
		sprintf(inserted_next, "#($%.*s = \"%.*s\")",
			BUFSIZE - 10, named_var->name,
			BUFSIZE - (int)strlen(named_var->name) - 10,
			pbuf ? ptrdata(pbuf) : (char *)"");
	    } else {
		sprintf(inserted_next, "#(@%.*s = %ld)",
			BUFSIZE - 8, named_var->name,
			*VAR[idx].num);
	    }
	} else {
	    if (type == TYPE_TXT_VAR) {
		pbuf = ptrescape(pbuf, *VAR[idx].str, 0);
		sprintf(inserted_next, "#($%d = \"%.*s\")", idx,
			BUFSIZE - INTLEN - 10,
			pbuf ? ptrdata(pbuf) : (char *)"");
	    } else {
		sprintf(inserted_next, "#(@%d = %ld)", idx,
			*VAR[idx].num);
	    }
	}
	ptrdel(pbuf);
	return;
    }
    
    /* only case left: assign a value to a variable */
    arg = redirect(expr, &pbuf, &kind, "var", 1, &start, &end);
    if (REAL_ERROR || !arg)
	return;

    if (kind) {
	char buf2[BUFSIZE];
	fp = (kind == '!') ? popen(arg, "r") : fopen(arg, "r");
	if (!fp) {
	    PRINTF("#var: #error opening \"%s\"\n", arg);
	    error=SYNTAX_ERROR;
	    ptrdel(pbuf);
	    return;
	}
	len = 0;
	i = 1;
	while (!error && (!start || i<=end) && fgets(buf2+len, BUFSIZE-len, fp))
	    if (!start || i++>=start)
		len += strlen(buf2 + len);
	
	if (kind == '!') pclose(fp); else fclose(fp);
	if (len>PARAMLEN)
	    len = PARAMLEN;
	buf2[len] = '\0';
	arg = buf2;
    }
    
    if (type == TYPE_NUM_VAR) {
	arg = skipspace(arg);
	type = 1;
	len = 0;
	
	if (*arg == '-')
	    arg++, type = -1;
	else if (*arg == '+')
	    arg++;
	
	if (isdigit(kind=*arg)) while (isdigit(kind)) {
	    len*=10;
	    len+=(kind-'0');
	    kind=*++arg;
	}
	else {
	    PRINTF("#var: ");
	    print_error(error=NO_NUM_VALUE_ERROR);
	}
	*VAR[idx].num = len * type;
    }
    else {
	*VAR[idx].str = ptrmcpy(*VAR[idx].str, arg, strlen(arg));
	if (MEM_ERROR)
	    print_error(error);
    }
    ptrdel(pbuf);
}

static void cmd_setvar __P1 (char *,arg)
{
    char *name;
    int i, func = 0; /* show */
    long buf;
    
    name = arg = skipspace(arg);
    arg = first_regular(arg, '=');
    if (*arg) {
	*arg++ = '\0';
	if (*arg) {
	    func = 1; /* set */
	    if (*arg == '(') {
		arg++;
		i = evall(&buf, &arg);
		if (!REAL_ERROR && i != TYPE_NUM)
		    error=NO_NUM_VALUE_ERROR;
		else if (!REAL_ERROR && *arg != ')')
		    error=MISSING_PAREN_ERROR;
	    } else
		buf = strtol(arg, NULL, 0);
	} else
	    buf = 0;
    
	if (REAL_ERROR) {
	    PRINTF("#setvar: ");
	    print_error(error);
	    return;
	}
    }
    
    i = strlen(name);
    if (i && !strncmp(name, "timer", i)) {
	vtime t;
	update_now();
	if (func == 0)
	    sprintf(inserted_next, "#setvar timer=%ld",
		    diff_vtime(&now, &ref_time));
	else {
	    t.tv_usec = ((-buf) % mSEC_PER_SEC) * uSEC_PER_mSEC;
	    t.tv_sec  =  (-buf) / mSEC_PER_SEC;
	    ref_time.tv_usec = now.tv_usec;
	    ref_time.tv_sec  = now.tv_sec;
	    add_vtime(&ref_time, &t);
	}
    }
    else if (i && !strncmp(name, "lines", i)) {
	if (func == 0)
	    sprintf(inserted_next, "#setvar lines=%d", lines);
	else {
	    if (buf > 0)
		lines = (int)buf;
	    if (opt_info) {
		PRINTF("#setvar: lines=%d\n", lines);
	    }
	}
    }
    else if (i && !strncmp(name, "mem", i)) {
	if (func == 0)
	    sprintf(inserted_next, "#setvar mem=%d", limit_mem);
	else {
	    if (buf == 0 || buf >= PARAMLEN)
		limit_mem = buf <= INT_MAX ? (int)buf : INT_MAX;
	    if (opt_info) {
		PRINTF("#setvar: mem=%d%s\n", limit_mem,
		       limit_mem ? "" : " (unlimited)");
	    }
	}
    }
    else if (i && !strncmp(name, "buffer", i)) {
	if (func == 0)
	    sprintf(inserted_next, "#setvar buffer=%d", log_getsize());
	else
	    log_resize(buf);
    } else {
	update_now();
	PRINTF("#setvar buffer=%d\n#setvar lines=%d\n#setvar mem=%d\n#setvar timer=%ld\n",
	       log_getsize(), lines, limit_mem, diff_vtime(&now, &ref_time));
    }
}

static void cmd_if __P1 (char *,arg)
{
    long buf;
    int type;
    
    arg = skipspace(arg);
    if (*arg!='(') {
	PRINTF("#if: ");
	print_error(error=MISMATCH_PAREN_ERROR);
	return;
    }
    arg++;  /* skip the '(' */
    
    type = evall(&buf, &arg);
    if (!REAL_ERROR) {
	if (type!=TYPE_NUM)
	    error=NO_NUM_VALUE_ERROR;
	if (*arg != ')')
	    error=MISSING_PAREN_ERROR;
	else {              /* skip the ')' */
	    if (*++arg == ' ')
		arg++;
	}
    }
    if (REAL_ERROR) {
	PRINTF("#if: ");
	print_error(error);
	return;
    }
    
    if (buf)
	(void)parse_instruction(arg, 0, 0, 1);
    else {
	arg = get_next_instr(arg);
	if (!strncmp(arg = skipspace(arg), "#else ", 6))
	    (void)parse_instruction(arg + 6, 0, 0, 1);
    }
}

static void cmd_for __P1 (char *,arg)
{
    int type = TYPE_NUM, loop=MAX_LOOP;
    long buf;
    char *check, *tmp, *increm = 0;
    
    arg = skipspace(arg);
    if (*arg != '(') {
	PRINTF("#for: ");
	print_error(error=MISMATCH_PAREN_ERROR);
	return;
    }
    push_params();
    if (REAL_ERROR)
	return;
    
    arg = skipspace(arg + 1);    /* skip the '(' */
    if (*arg != CMDSEP)
	(void)evaln(&arg);       /* execute <init> */
    
    check = arg + 1;
    
    if (REAL_ERROR)
	;
    else if (*arg != CMDSEP) {
	PRINTF("#for: ");
	print_error(error=MISSING_SEPARATOR_ERROR);
    }
    else while (!error && loop
		&& (increm=check, (type = evall(&buf, &increm)) == TYPE_NUM
		    && !error && *increm == CMDSEP && buf)) {
	
	tmp = first_regular(increm + 1, ')');
	if (*tmp)
	    (void)parse_instruction(tmp + 1, 1, 1, 1);
	else {
	    PRINTF("#for: ");
	    print_error(error=MISSING_PAREN_ERROR);
	}
	
	if (!error) {
	    tmp = increm + 1;
	    if (*tmp != ')')
		(void)evaln(&tmp);
	}
	
	loop--;
    }
    if (REAL_ERROR)
	;
    else if (increm && *increm != CMDSEP)
	error=MISSING_SEPARATOR_ERROR;
    else if (!loop)
	error=MAX_LOOP_ERROR;
    else if (type != TYPE_NUM)
	error=NO_NUM_VALUE_ERROR;
    if (REAL_ERROR) {
	PRINTF("#for: ");
	print_error(error);
    }
    if (error!=DYN_STACK_UND_ERROR && error!=DYN_STACK_OV_ERROR)
	pop_params();
}

static void cmd_while __P1 (char *,arg)
{
    int type = TYPE_NUM, loop=MAX_LOOP;
    long buf;
    char *check, *tmp;
    
    arg = skipspace(arg);
    if (!*arg) {
	PRINTF("#while: ");
	print_error(error=MISMATCH_PAREN_ERROR);
	return;
    }
    push_params();
    
    check = ++arg;   /* skip the '(' */
    while (!error && loop
	   && (arg=check, (type = evall(&buf, &arg)) == TYPE_NUM &&
	       !error && *arg == ')' && buf)) {
	
	if (*(tmp = arg + 1) == ' ')          /* skip the ')' */
	    tmp++;
	if (*tmp)
	    (void)parse_instruction(tmp, 1, 1, 1);
	loop--;
    }
    if (REAL_ERROR)
	;
    else if (*arg != ')')
	error=MISSING_PAREN_ERROR;
    else if (!loop)
	error=MAX_LOOP_ERROR;
    else if (type != TYPE_NUM)
	error=NO_NUM_VALUE_ERROR;
    if (REAL_ERROR) {
	PRINTF("#while: ");
	print_error(error);
    }
    if (error!=DYN_STACK_UND_ERROR && error!=DYN_STACK_OV_ERROR)
	pop_params();
}

static void cmd_capture __P1 (char *,arg)
{
    arg = skipspace(arg);
    
    if (!*arg) {
        if (capturefile) {
	    log_flush();
            fclose(capturefile);
            capturefile = NULL;
            if (opt_info) {
		PRINTF("#end of capture to file.\n");
            }
        } else {
            PRINTF("#capture to what file?\n");
        }
    } else {
        if (capturefile) {
            PRINTF("#capture already active.\n");
        } else {
	    short append = 0;
	    /* Append to log file, if the name starts with '>' */
	    if (*arg == '>') { 
		    arg++;
		    append = 1;
	    }
            if ((capturefile = fopen(arg, (append) ? "a" : "w")) == NULL) {
                PRINTF("#error writing file \"%s\"\n", arg);
            } else if (opt_info) {
                PRINTF("#capture to \"%s\" active, \"#capture\" ends.\n", arg);
            }
        }
    }
}

static void cmd_movie __P1 (char *,arg)
{
    arg = skipspace(arg);
    
    if (!*arg) {
        if (moviefile) {
	    log_flush();
            fclose(moviefile);
            moviefile = NULL;
            if (opt_info) {
		PRINTF("#end of movie to file.\n");
            }
        } else {
            PRINTF("#movie to what file?\n");
        }
    } else {
        if (moviefile) {
            PRINTF("#movie already active.\n");
        } else {
            if ((moviefile = fopen(arg, "w")) == NULL) {
                PRINTF("#error writing file \"%s\"\n", arg);
            } else {
		if (opt_info) {
		    PRINTF("#movie to \"%s\" active, \"#movie\" ends.\n", arg);
		}
		update_now();
		movie_last = now;
		log_clearsleep();
            }
        }
    }
}

static void cmd_record __P1 (char *,arg)
{
    arg = skipspace(arg);
    
    if (!*arg) {
        if (recordfile) {
            fclose(recordfile);
            recordfile = NULL;
            if (opt_info) {
		PRINTF("#end of record to file.\n");
            }
        } else {
            PRINTF("#record to what file?\n");
        }
    } else {
        if (recordfile) {
            PRINTF("#record already active.\n");
        } else {
            if ((recordfile = fopen(arg, "w")) == NULL) {
                PRINTF("#error writing file \"%s\"\n", arg);
            } else if (opt_info) {
                PRINTF("#record to \"%s\" active, \"#record\" ends.\n", arg);
            }
        }
    }
}

static void cmd_edit __P1 (char *,arg)
{
    editsess *sp;
    
    if (edit_sess) {
        for (sp = edit_sess; sp; sp = sp->next) {
	    PRINTF("# %s (%u)\n", sp->descr, sp->key);
	}
    } else {
	PRINTF("#no active editors.\n");
    }
}

static void cmd_cancel __P1 (char *,arg)
{
    editsess *sp;
    
    if (!edit_sess) {
        PRINTF("#no editing sessions to cancel.\n");
    } else {
        if (*arg) {
            for (sp = edit_sess; sp; sp = sp->next)
		if (strtoul(arg, NULL, 10) == sp->key) {
		    cancel_edit(sp);
		    break;
		}
            if (!sp) {
                PRINTF("#unknown editing session %d\n", atoi(arg));
            }
        } else {
            if (edit_sess->next) {
                PRINTF("#several editing sessions active, use #cancel <number>\n");
            } else
		cancel_edit(edit_sess);
        }
    }
}

static void cmd_net __P1 (char *,arg)
{
    PRINTF("#received from host: %ld chars, sent to host: %ld chars.\n",
	       received, sent);
}


#ifndef CLOCKS_PER_SEC
#  define CLOCKS_PER_SEC uSEC_PER_SEC
#endif
/* hope it works.... */

static void cmd_cpu __P1 (char *,arg)
{
    float f, l;
    update_now();
    f = (float)((cpu_clock = clock()) - start_clock) / (float)CLOCKS_PER_SEC;
    l = (float)(diff_vtime(&now, &start_time)) / (float)mSEC_PER_SEC;
    PRINTF("#CPU time used: %.3f sec. (%.2f%%)\n", f,
	       (l > 0 && l > f) ? f * 100.0 / l : 100.0);
}

void show_stat __P0 (void)
{
    cmd_net(NULL);
    cmd_cpu(NULL);
}

#ifdef BUG_TELNET
static void cmd_color __P1 (char *,arg)
{
    int attrcode;
    
    arg = skipspace(arg);
    if (!*arg) {
        strcpy(tty_modenorm, tty_modenormbackup);
	tty_puts(tty_modenorm);	
	if (opt_info) {
	    PRINTF("#standard color cleared.\n");
	}
	return;
    }
    
    attrcode = parse_attributes(arg);
    if (attrcode == -1) {
        PRINTF("#invalid attribute syntax.\n");
	if (opt_info)
	    show_attr_syntax();
    } else {
        int bg = BACKGROUND(attrcode), fg = FOREGROUND(attrcode);
        if (fg >= COLORS || bg >= COLORS) {
	    PRINTF("#please specify foreground and background colors.\n");
	} else {
	    sprintf(tty_modenorm, "\033[;%c%d;%s%dm",
		    fg<LOWCOLORS ? '3' : '9', fg % LOWCOLORS,
		    bg<LOWCOLORS ? "4" :"10", bg % LOWCOLORS);
	    tty_puts(tty_modenorm);
	    if (opt_info) {
	        PRINTF("#standard colour set.\n");
	    }
        }
    }
}
#endif

static void cmd_connect __P1 (char *,arg)
{
#ifdef TERM
    PRINTF("#connect: multiple connections not supported in term version.\n");
#else
    char *s1, *s2, *s3 = NULL, *s4 = NULL;
    int argc = 1;
    
    if (!*skipspace(arg)) {
	tcp_show();
	return;
    }
    else {
	s1 = strtok(arg, " ");
	s2 = strtok(NULL, " ");
	if (s2 && *s2) {
	    argc++;
	    s3 = strtok(NULL, " ");
	    if (s3 && *s3) {
		argc++;
		s4 = strtok(NULL, " ");
		if (s4 && *s4)
		    argc++;
	    }
	}
    }
    
    if (argc <= 2) {
	if (*hostname)
	    tcp_open(s1, s2, hostname, portnumber);
	else {
	    PRINTF("#connect: no host defined!\n#syntax: #connect session-id [[init-str] [hostname] [port]]\n");
	}
    } else if (argc == 3) {
	if (!*hostname) {
	    my_strncpy(hostname, s2, BUFSIZE-1);
	    portnumber = atoi(s3);
	}
	tcp_open(s1, NULL, s2, atoi(s3));
    } else {
	if (!*hostname) {
	    my_strncpy(hostname, s3, BUFSIZE-1);
	    portnumber = atoi(s4);
	}
	tcp_open(s1, s2, s3, atoi(s4));
    }
#endif /* TERM */
}


static void cmd_spawn __P1 (char *,arg)
{
    char s[BUFSIZE];
    if (*(arg = skipspace(arg))) {
	arg = split_first_word(s, BUFSIZE, arg);
	if (*arg && *s) {
	    tcp_spawn(s, arg);
	    return;
	}
    }
    PRINTF("#syntax: #spawn connect-id command\n");
}

/* If you have speedwalk off but still want to use a speedwalk sequence,
 * you can manually trigger a speedwalk this way */
static void cmd_speedwalk __P1 (char *,arg)
{
    char save_speedwalk = opt_speedwalk;
    PRINTF( "Executing speedwalk '%s'\n", arg );
    opt_speedwalk = 1;
    if( ! map_walk( skipspace(arg), 0, 0 ) ) {
        PRINTF( "Error executing speedwalk\n" );
    }
    opt_speedwalk = save_speedwalk;
}

static void cmd_zap __P1 (char *,arg)
{
    if (!*arg) {
	PRINTF("#zap: no connection name.\n");
    } else
	tcp_close(arg);
}

static void cmd_qui __P1 (char *,arg)
{
    PRINTF("#you have to write '#quit' - no less, to quit!\n");
}

static void cmd_quit __P1 (char *,arg)
{
    if (*arg) { /* no skipspace() here! */
	PRINTF("#quit: spurious argument?\n");
    } else
	exit_powwow();
}

static const struct {
    const char *name;
    char *option;
    const char *doc;
} options[] = {
    { "autoclear", &opt_autoclear,
      "clear input line before executing commands" },
    { "autoprint", &opt_autoprint,
      "#print lines matched by actions" },
    { "compact",   &opt_compact,
      "remove prompt when receiving new messages from mud" },
    { "debug",     &opt_debug,
      "print commands before executing" },
    { "echo",      &opt_echo,
      "print command action commands when executed" },
    { "exit",      &opt_exit,
      "automatically exit powwow when mud connection closes" },
    { "history",   &opt_history,
      "also save command history" },
    { "info",      &opt_info,
      "print information about command effects" },
    { "keyecho",   &opt_keyecho,
      "print command bound to key when executed" },
    { "reprint",   &opt_reprint,
      "reprint sent commands when getting new prompt" },
    { "sendsize",  &opt_sendsize,
      "send terminal size when opening connection" },
    { "speedwalk", &opt_speedwalk,
      "enable speed walking (ness3ew...)" },
    { "words",     &opt_words,
      "also save word history" },
    { "wrap",      &opt_wrap,
      "enable word wrapping" },
    { NULL }
};

/* print all options to 'file', or tty if file is NULL; return -1 on
 * error, 1 on success */
int print_all_options __P1 (FILE *,file)
{
    const char *prefix = "#option";
    int width = (file ? 80 : cols) - 16;
    int len = 0, i;
    for (i = 0; options[i].name; ++i) {
        int res;
        if (file)
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
    if (file) {
        fputc('\n', file);
    } else {
        tty_putc('\n');
        status(1);
    }
    return 1;
}

static void cmd_option __P1 (char *,arg)
{
    char buf[BUFSIZE];
    int count = 0;

    arg = skipspace(arg);
    if (!*arg) {
        print_all_options(NULL);
        return;
    }

    while ((arg = skipspace(split_first_word(buf, BUFSIZE, arg))), *buf) {
        enum { MODE_ON, MODE_OFF, MODE_TOGGLE, MODE_REP } mode;
        char *varp = NULL;
        char *p = buf;
        char c = *p;
        int len = strlen(p);
        int i;

        switch (c) {
        case '=': mode = MODE_REP; p++; break;
        case '+': mode = MODE_ON;  p++; break;
        case '-': mode = MODE_OFF; p++; break;
        default:  mode = MODE_TOGGLE;     break;
        }
        count++;
        for (i = 0; options[i].name; i++) {
            if (strncmp(options[i].name, p, len) == 0) {
                varp = options[i].option;
                break;
            }
        }
        if (varp == NULL) {
            if (strncmp("list", p, len) == 0) {
                tty_puts("#list of options:\n");
                for (i = 0; options[i].name; ++i) {
                    tty_printf("#option %c%-12s    %s\n",
                               *options[i].option ? '+' : '-',
                               options[i].name,
                               options[i].doc);
                }
            } else {
                tty_puts("#syntax: #option [[+|-|=]<name>] | list\n");
            }
            status(1);
            return;
        }

        switch (mode) {
        case MODE_REP:
            sprintf(inserted_next, "#option %c%s", *varp ? '+' : '-',
                    p);
            break;
        case MODE_ON:     *varp  = 1; break;
        case MODE_OFF:    *varp  = 0; break;
        case MODE_TOGGLE: *varp ^= 1; break;
        }
        /*
         * reset the reprint buffer if changing its status
         */
        if (varp == &opt_reprint)
            reprint_clear();

        /* as above, but always print status if
         * "#option info" alone was typed */
        if (mode != MODE_REP && !*arg && count==1 &&
            (opt_info || (mode == MODE_TOGGLE && varp==&opt_info))) {
            PRINTF("#option %s is now o%s.\n",
                   options[i].name,
                   *varp ? "n" : "ff");
        }
    }
}

static void cmd_file __P1 (char *,arg)
{
    arg = skipspace(arg);
    if (*arg == '=') {
	set_deffile(++arg);
	if (opt_info) {
	    if (*arg) {
		PRINTF("#save-file set to \"%s\"\n", deffile);
	    } else {
		PRINTF("#save-file is now undefined.\n");
	    }
	}
    } else if (*deffile) {
	sprintf(inserted_next, "#file =%.*s", BUFSIZE-8, deffile);
    } else {			  
	PRINTF("#save-file not defined.\n");
    }
}

static void cmd_save __P1 (char *,arg)
{ 
    arg = skipspace(arg);
    if (*arg) {
	set_deffile(arg);
	if (opt_info) {
	    PRINTF("#save-file set to \"%s\"\n", deffile);
	}
    } else if (!*deffile) {
	PRINTF("#save-file not defined.\n");
	return;
    }
    
    if (*deffile && save_settings() > 0 && opt_info) {
	PRINTF("#settings saved to file.\n");
    }
}

static void cmd_load __P1 (char *,arg)
{
    int res;
    
    arg = skipspace(arg);
    if (*arg) {
	set_deffile(arg);
	if (opt_info) {
	    PRINTF("#save-file set to \"%s\"\n", deffile);
	}
    }
    else if (!*deffile) {
	PRINTF("#save-file not defined.\n");
	return;
    }
    
    res = read_settings();
    
    if (res > 0) {
	/* success */
	if (opt_info) {
	    PRINTF("#settings loaded from file.\n");
	}
    } else if (res < 0) {
	/* critical error */
	while (keydefs)
	    delete_keynode(&keydefs);
	tty_add_initial_binds();
	tty_add_walk_binds();
	limit_mem = 1048576;
	PRINTF("#emergency loaded default settings.\n");
    }
}

static char *trivial_eval __P3 (ptr *,pbuf, char *,arg, char *,name)
{
    char *tmp = skipspace(arg);
    
    if (!pbuf)
	return NULL;
    
    if (*tmp=='(') {
	arg = tmp + 1;
	(void)evalp(pbuf, &arg);
	if (!REAL_ERROR && *arg != ')')
	    error=MISSING_PAREN_ERROR;
	if (REAL_ERROR) {
	    PRINTF("#%s: ", name);
	    print_error(error);
	    return NULL;
	}
	if (*pbuf)
	    arg = ptrdata(*pbuf);
	else
	    arg = "";
    }
    else
	unescape(arg);
    
    return arg;
}

static void do_cmd_add __P2(char *,arg, int,is_static)
{
    ptr pbuf = (ptr)0;
    char buf[BUFSIZE];
    
    arg = trivial_eval(&pbuf, arg, "add");
    if (!REAL_ERROR)
	while (*arg) {
	    arg = split_first_word(buf, BUFSIZE, arg);
	    if (strlen(buf) >= MIN_WORDLEN)
		(is_static ? put_static_word : put_word)(buf);
	}
    ptrdel(pbuf);
}

static void cmd_add __P1 (char *,arg)
{
    do_cmd_add(arg, 0);
}

static void cmd_addstatic __P1 (char *,arg)
{
    do_cmd_add(arg, 1);
}

static void cmd_put __P1 (char *,arg)
{
    ptr pbuf = (ptr)0;
    arg = trivial_eval(&pbuf, arg, "put");
    if (!REAL_ERROR && *arg)
	put_history(arg);
    ptrdel(pbuf);
}


