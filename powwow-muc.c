#include <stdlib.h>
#include <curses.h>
#include <stdio.h>
#include <string.h>

/* Curses based powwow movie player.
 * Author: Steve Slaven - http://hoopajoo.net
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 */

#define JUMP_SMALL     1000
#define JUMP_BIG       5000
#define MAX_SPEED      20

/* Speed is a variable from 0 - 9, where 5 = normal and > 5 is faster */
int main( int argc, char *argv[] ) {
	WINDOW *text, *status;
	int speed = 5;
	int key, sleep, orig, i, color, looping, cursx, cursy;
	FILE *in;
	char line[ 1000 ], *displine, action[ 100 ];
	long curr_pos, file_size, new_pos;

	if( argc < 2 ) {
		fprintf( stderr,
			"powwow mud cinema v%s\n"
			"Author: Steve Slaven - http://hoopajoo.net\n"
			"\n"
			"Usage: powwow-muc file\n"
			"\n"
			"file should be a #movie file generated in powwow\n",
			VERSION );
		exit( 1 );
	}

	/* Validate our file passed */
	if( ! ( in = fopen( argv[ 1 ], "ro" ) ) ) {
		perror( "Unable to open file" );
		exit( 1 );
	}

	/* Get file size */
	fseek( in, 0, SEEK_END );
	file_size = ftell( in );
	rewind( in );

	/* Setup some basic stuff */
	initscr();
	cbreak();
	noecho();

	/* Initialize color */
	start_color();

	/* this *should* init the default ansi colors... :/ */
	for( i = 0; i < 16; i++ )
		init_pair( i, i, COLOR_BLACK );

	/* Create our windows, a status bar on the bottom and a
	   scrollable window on top */
	text = newwin( LINES - 2, COLS, 0, 0 );
	status = newwin( 2, COLS, LINES - 2, 0 );

	keypad( stdscr, TRUE );
	scrollok( text, TRUE );
	leaveok( text, TRUE );
	leaveok( status, TRUE );
	/* wattron( status, A_BOLD ); */
	wbkgd( status, A_REVERSE );

	/* instructions */
	mvwprintw( status, 0, 0,
		"(q)uit (r)ew small (R)ew large (f)orw small (F)orw large (0-9)(+)(-) speed" );

	/* Main loop */
	refresh();
	timeout( 0 );
	looping = 1;
	while( looping ) {
		memset( line, 0, sizeof line );
		fgets( line, sizeof line, in );

		/* get file pos */
		new_pos = curr_pos = ftell( in );

		/* handle disp or other */
		displine = NULL;
		if( strncmp( line, "line", 4 ) == 0 ) {
			displine = &line[ 5 ];
			sleep = 0;
		}else if( strncmp( line, "prompt", 5 ) == 0 ) {
			displine = &line[ 7 ];
			/* Munch newline */
			line[ strlen( line ) - 1 ] = 0;
			sleep = 0;
		}else if( strncmp( line, "sleep", 5 ) == 0 ) {
			sscanf( line, "sleep %d", &sleep );
		}else if( line[ 0 ] == '#' ) { /* custom extension for commenting logs */
			sscanf( line, "#%d", &sleep );
			if( sleep > 0 )
				sleep *= 100; /* comment sleep is in seconds */

			/* Skip to space */
			displine = line;
			while( displine[ 0 ] != ' ' && displine[ 0 ] != 0 ) {
				displine++;
			}
			displine++;

			/* We will go ahead and display it here, in bold, then
			   null out displine so it will not display later */
			wattron( text, A_REVERSE );
			wprintw( text, "##==> %s", displine );
			wattroff( text, A_REVERSE );
			displine = NULL;
		}

		/* suck out action for display */
		sscanf( line, "%99s", action );

		/* Modify sleep time according to speed, zero is fast as you can go, 1 == pause */
		orig = sleep;
		if( speed > 5 ) {
			sleep 	 /= ( ( speed - 5 ) * 2 );
		}else{
			sleep *= ( 6 - speed );
		}

		/* Handle pause */
		if( speed == 0 )
			sleep = -1;

		/* handle insane speed */
		/* if( speed == 9 )
			sleep = 0; */

		/* Setup sleeptime for getch() */
		timeout( sleep );

		/* Update status line */
		mvwprintw( status, 1, 0,
			"%7d/%7d/%2d%% Speed: %d (5=normal,0=pause) Cmd: %-6s (%d/%d)\n",
			curr_pos, file_size, curr_pos * 100 / file_size,
			speed, action, sleep, orig );
		wrefresh( status );

		/* Disp if we found an offset to do */
		if( displine != NULL ) {
			/* handle converting ansi colors to curses attrs */
			for( i = 0; i < strlen( displine ); i++ ) {
				if( displine[ i ] == 0x1b ) {
					/* this is super crappy ansi color decoding */
					i++;
					if( strncmp( &displine[ i ], "[3", 2 ) == 0 ) {
						/* start a color */
						sscanf( &displine[ i ], "[3%dm", &color );
						wattron( text, COLOR_PAIR( color ) );
					}else if( strncmp( &displine[ i ], "[9", 2 ) == 0 ) {
						/* start a high color */
						sscanf( &displine[ i ], "[9%dm", &color );
						wattron( text, COLOR_PAIR( color ) );
						wattron( text, A_BOLD );
					}else if( strncmp( &displine[ i ], "[1", 2 ) == 0 ) {
						wattron( text, A_BOLD );
					}else if( strncmp( &displine[ i ], "[0", 2 ) == 0 ) {
						/* end color, color will (should?) still be set from last color */
						/* wattr_off( text, COLOR_PAIR( color ), NULL ); */
						wattrset( text, A_NORMAL );
					}
					/* eat chars to the next m */
					while( displine[ i ] != 'm' && displine != 0 )
						i++;
				}else{
					waddch( text, (unsigned char)displine[ i ] );
				}
			}
		}

		/* check if we are at EOF and override timeout */
		if( curr_pos == file_size ) {
			wprintw( text, "\n**** END ***\n" );
			timeout( -1 );
		}

		wrefresh( text );

		/* Move the cursor to the end of the text window, so it looks
		   like a real session */
		getyx( text, cursy, cursx );
		move( cursy, cursx );

		key = getch();

		switch( key ) {
			case 'Q':
			case 'q':
				looping = 0;
				break;
			case '+':
			case '=':
				speed++;
				break;
			case '-':
				speed--;
				break;

			case '1':
				speed = 1;
				break;
			case '2':
				speed = 2;
				break;
			case '3':
				speed = 3;
				break;
			case '4':
				speed = 4;
				break;
			case '5':
				speed = 5;
				break;
			case '6':
				speed = 6;
				break;
			case '7':
				speed = 7;
				break;
			case '8':
				speed = 8;
				break;
			case '9':
				speed = 9;
				break;
			case '0':
				speed = 0;
				break;

			case 'r':
				new_pos -= JUMP_SMALL;
				break;
			case 'R':
				new_pos -= JUMP_BIG;
				break;
			case 'f':
				new_pos += JUMP_SMALL;
				break;
			case 'F':
				new_pos += JUMP_BIG;
				break;

			default:
				break;
		}

		/* Validate speed is ok */
		if( speed > MAX_SPEED )
			speed = MAX_SPEED;

		if( speed < 0 )
			speed = 0;

		/* Check if we are moving the seek */
		if( new_pos != curr_pos ) {
			wattron( text, A_BOLD );
			if( new_pos > file_size )
				new_pos = file_size;

			if( new_pos < 0 )
				new_pos = 0;

			wprintw( text,
				"\n=============\nMoving from %d to file offset %d\n",
				curr_pos, new_pos );

			/* calcs offsets because we may want to seek to
			   0, which using SEEK_CUR won't let us do without
			   some other error checking that I don't want to do */
			fseek( in, new_pos, SEEK_SET );

			/* read to next newline so we don't break up
			   lines seeking around */
			fgets( line, sizeof line, in );
			new_pos = ftell( in );

			/* Make a note of moving */
			wprintw( text,
				"Final offset after adjusting for newline: %d (%d)\n=============\n",
				new_pos, new_pos - curr_pos );
			wattroff( text, A_BOLD );
		}
	}

	/* Cleanup */
	delwin( text );
	delwin( status );
	endwin();

	fclose( in );

	return( 0 );
}
