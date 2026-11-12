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

static void plugtest(char *arg);

static cmdstruct mycommand = {
    .sortname = NULL, .name = "plugtest", .help = "test command",
    .funct = plugtest, .next = NULL
};

/* found using dlsym(), so it cannot be static */
void powwow_init(void)
{
    tty_printf("Init plugtest.so!\n");

    cmd_add_command(&mycommand);
}

static void plugtest(char *arg)
{
    tty_printf("Arg was '%s'\n", arg);
}
