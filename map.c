/*
 *  map.c  --  mapping routines.
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
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <sys/types.h>
#include <sys/time.h>

#include "defines.h"
#include "main.h"
#include "tty.h"
#include "edit.h"
#include "tcp.h"

/*
 * mapping variables
 */
static char mappath[MAX_MAPLEN]; /* circular path list */
static int mapstart = 0;	/* index to first map entry */
static int mapend = 0;		/* index one past last map entry */

#define MAPINDEX(i) (((i) + MAX_MAPLEN) % MAX_MAPLEN)
#define MAPENTRY(i) mappath[MAPINDEX(i)]

/*
 * return reverse direction
 */
static char reverse_dir __P1 (char,dir)
{
    static char dirs[] = "nsewud";
    char *p = strchr(dirs, dir);
    return p ? dirs[(p - dirs) ^ 1] : 0;
}

/*
 * retrace steps on map, optionally walk them back
 */
void map_retrace __P2 (int,steps, int,walk_back)
{
    char cmd[2];
    
    cmd[1] = '\0';
    
    if (!steps && !walk_back)
	mapend = mapstart;
    else {
	if (!steps)
	    steps = -1;
	if (walk_back && echo_ext) {
	    status(1);
	    tty_putc('[');
	}
	
	while (mapstart != mapend && steps--) {
	    mapend = MAPINDEX(mapend - 1);
	    if (walk_back) {
		cmd[0] = reverse_dir(mappath[mapend]);
		if (echo_ext)
		    tty_putc(cmd[0]);
		tcp_write(tcp_fd, cmd);
	    }
	}
	if (walk_back && echo_ext)
	    tty_puts("]\n");
    }
}

/*
 * show automatic map (latest steps) in the form s2ews14n
 */
void map_show __P0 (void)
{
    char lastdir;
    int count = 0;
    int i;

    if (mapstart == mapend) {
        PRINTF("#map empty\n");
    } else {
        PRINTF("#map: ");

	lastdir = mappath[mapstart];
	for (i = mapstart; i != mapend; i = MAPINDEX(i + 1)) {
	    if (mappath[i] != lastdir) {
		if (count > 1)
		    tty_printf("%d", count);
		tty_putc(lastdir);

		count = 1;
		lastdir = mappath[i];
	    } else
		count++;
	}

	if (count > 1)
	    tty_printf("%d", count);

	tty_printf("%c\n", lastdir);
    } 
}

/*
 * print map to string in the form seewsnnnnn
 */
void map_sprintf __P1 (char *,buf)
{
    int i;

    if (mapstart != mapend)
	for (i = mapstart; i != mapend; i = MAPINDEX(i + 1))
	    *buf++ = mappath[i];
    *buf = '\0';
}

/*
 * add direction to automap
 */
void map_add_dir __P1 (char,dir)
{
#ifdef NOMAZEMAPPING
    if (mapend != mapstart && dir == reverse_dir(MAPENTRY(mapend - 1))) {
	mapend = MAPINDEX(mapend - 1); /* retrace one step */
    } else
#endif
    {
	MAPENTRY(mapend) = dir;
	mapend = MAPINDEX(mapend + 1);
	if(mapend == mapstart)
	    mapstart = MAPINDEX(mapstart + 1);
    }
}

/*
 * execute walk if word is valid [speed]walk sequence -
 * return 1 if walked, 0 if not
 */
int map_walk __P3 (char *,word, int,silent, int,maponly)
{
    char buf[16];
    int n = strlen(word);
    int is_main = (tcp_fd == tcp_main_fd);
    
    if (!is_main && !maponly && !opt_speedwalk)
	return 0;
    if (!n || (n > 1 && !opt_speedwalk && !maponly) ||
	!strchr("neswud", word[n - 1]) ||
	(int)strspn(word, "neswud0123456789") != n)
	return 0;
    
    if (maponly)
	silent = 1;
    buf[1] = '\0';
    while (*word) {
        if (!silent) { status(1); tty_putc('['); }
	
        if (isdigit(*word)) {
            n = strtol(word, &word, 10);
	    if (!silent)
		tty_printf("%d", n);
	} else
	    n = 1;
        if (!silent)
	    tty_putc(*word);
        while (n--) {
            *buf = *word;
            if (!maponly) {
	        tcp_write(tcp_fd, buf);
	    }
            if (is_main || maponly)
		map_add_dir(*word);
        }
	if (!silent)
	    tty_puts("] ");
        word++;
    }
    if (!silent)
	tty_putc('\n');
    return !maponly;
}

