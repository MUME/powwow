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

#ifndef USE_SGTTY
# ifdef APOLLO
#  include "/sys5.3/usr/include/sys/termio.h"
# else
/*
 * including both termio.h and termios.h might be an overkill, and gives
 * many warnings, but seems to be necessary at times. works anyway.
 */
#  include <termios.h>
#  include <termio.h>
# endif
/* #else USE_SGTTY */
#endif

/*
 * SunOS 4 doesn't have function headers and has the defs needed from
 * ioctl.h in termios.h.  Does it compile with USE_SGTTY?
 */
#if (defined(sun) && defined(sparc) && ! defined(__SVR4))
extern int printf();
#else
# include <sys/ioctl.h>
#endif

#ifdef BSD_LIKE
# include <sys/ioctl_compat.h>
# define O_RAW RAW
# define O_ECHO ECHO
# define O_CBREAK CBREAK
#endif

#if defined(TCSETS) || defined(TCSETATTR)
# ifndef TCSETS		/* cc for HP-UX  SHOULD define this... */
#  define TCSETS TCSETATTR
#  define TCGETS TCGETATTR
# endif
typedef struct termios termiostruct;
#else
# define TCSETS TCSETA
# define TCGETS TCGETA
typedef struct termio termiostruct;
#endif

#ifdef VSUSP
# define O_SUSP VSUSP
#else
# ifdef SWTCH
#  define O_SUSP SWTCH
# else
#  define O_SUSP SUSP
# endif
#endif

/*int ioctl();*/

#ifdef USE_SGTTY
static struct sgttyb ttybsave;
static struct tchars tcsave;
static struct ltchars ltcsave;
#else /* not USE_SGTTY */
static termiostruct ttybsave;
#endif /* USE_SGTTY */

/*
 * Terminal handling routines:
 * These are one big mess of left-justified chicken scratches.
 * It should be handled more cleanly...but unix portability is what it is.
 */

/*
 * Set the terminal to character-at-a-time-without-echo mode, and save the
 * original state in ttybsave
 */
void set_terminal()
{
#ifdef USE_SGTTY
    struct sgttyb ttyb;
    struct ltchars ltc;
    ioctl(0, TIOCGETP, &ttybsave);
    ioctl(0, TIOCGETC, &tcsave);
    ioctl(0, TIOCGLTC, &ltcsave);
    ttyb = ttybsave;
    ttyb.sg_flags = (ttyb.sg_flags|O_CBREAK) & ~O_ECHO;
    ioctl(0, TIOCSETP, &ttyb);
    ltc = ltcsave;
    ltc.t_suspc = -1;
    ioctl(0, TIOCSLTC, &ltc);
#else /* not USE_SGTTY */
    termiostruct ttyb;
    ioctl(0, TCGETS, &ttyb);
    ttybsave = ttyb;
    ttyb.c_lflag &= ~(ECHO|ICANON);
    ttyb.c_cc[VTIME] = 0;
    ttyb.c_cc[VMIN] = 1;
    /* disable the special handling of the suspend key (handle it ourselves) */
    ttyb.c_cc[O_SUSP] = 0;
    ioctl(0, TCSETS, &ttyb);
#endif /* USE_SGTTY */
}

/*
 * Reset the terminal to its original state
 */
void reset_terminal()
{
#ifdef USE_SGTTY
    ioctl(0, TIOCSETP, &ttybsave);
    ioctl(0, TIOCSETC, &tcsave);
    ioctl(0, TIOCSLTC, &ltcsave);
#else /* not USE_SGTTY */
    ioctl(0, TCSETS, &ttybsave);
#endif /* USE_SGTTY */
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
	read(0, &c, 1);
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

