#include <stdio.h>

#include "defines.h"
#include "cmd.h"
#include "tty.h"

/* Bare test plugin for powwow
 * Author: Steve Slaven - http://hoopajoo.net
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 */

void plugtest( char *arg );

cmdstruct mycommand = { NULL, "plugtest", "test command", plugtest, NULL };

void powwow_init() {
	tty_printf( "Init plugtest.so!\n" );

	cmd_add_command( &mycommand );
}

void plugtest( char *arg ) {
	tty_printf( "Arg was '%s'\n", arg );
}
