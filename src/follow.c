/*
 *  follow.c  --  interactively print an ASCII file.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/types.h>

#include "defines.h"

#include <termios.h>

static struct termios ttybsave;

/*
 * Set the terminal to character-at-a-time-without-echo mode, and save the
 * original state in ttybsave
 */
static void set_terminal()
{
    tcgetattr(0, &ttybsave);
    struct termios ttyb = ttybsave;
    ttyb.c_lflag &= ~(ECHO | ICANON);
    ttyb.c_cc[VTIME] = 0;
    ttyb.c_cc[VMIN] = 1;
    /* disable the special handling of the suspend key (handle it ourselves) */
    ttyb.c_cc[VSUSP] = _POSIX_VDISABLE;
    tcsetattr(0, TCSANOW, &ttyb);
}

/*
 * Reset the terminal to its original state
 */
static void reset_terminal()
{
    tcsetattr(0, TCSANOW, &ttybsave);
}

int main(int argc, char *argv[]) {
    FILE *f;
    char c = 0, buf[512];
    int d;

    if (argc < 2) {
	fprintf(stderr, "needed a file name\n");
	exit(0);
    }
    f = fopen(argv[1], "r");
    if (!f) {
	fprintf(stderr, "unable to open %s\n", argv[1]);
	exit(0);
    }

    set_terminal();
    while(c!=0x1b) {
	if (read(0, &c, 1) != 1)
	    break;
	if (c == 0x0a || c == 0x0d) {
	    if (fgets(buf, 512, f))
		fputs(buf, stdout);
	    else
		break;
	}
	else {
	    if ((d = fgetc(f)) != EOF)
		putchar(d);
	    else
		break;
	}
	fflush(stdout);
    }
    reset_terminal();
    fputs("\033[0m\n", stdout);
    return 0;
}

