#include <stdio.h>

#include "defines.h"
#include "cmd.h"

void plugtest( char *arg );

cmdstruct mycommand = { NULL, "plugtest", "test command", plugtest, NULL };

void powwow_init() {
	printf( "Init plugtest.so!\n" );

	cmd_add_command( &mycommand );
}

void plugtest( char *arg ) {
	printf( "Arg was '%s'\n", arg );
}
