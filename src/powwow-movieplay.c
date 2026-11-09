/*
 *  powwow-movieplay.c  --  replay powwow movies or convert them to text
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

static void millisec_sleep(long msec)
{
    struct timeval t;
    t.tv_sec = msec / 1000;
    t.tv_usec = (msec % 1000) * 1000;
    select(0, NULL, NULL, NULL, &t);
}

/*
 * replay the movie in the file named by the first argument, or standard
 * input, on the file named by the second, or standard output, at the
 * speed it was recorded at, or with -a at once, as text
 */
int main(int argc, char *argv[])
{
    FILE *infile = stdin, *outfile = stdout;
    char buf[4096];
    int i;
    bool play = true;

    int opt;
    while ((opt = getopt(argc, argv, "a")) != -1) {
        if (opt == 'a') {
            play = false;
        } else {
            fprintf(stderr, "Usage: %s [-a] [infile [outfile]]\n", argv[0]);
            return 1;
        }
    }
    if (argc - optind > 2) {
        fprintf(stderr, "Usage: %s [-a] [infile [outfile]]\n", argv[0]);
        return 1;
    }
    if (optind < argc) {
        infile = fopen(argv[optind], "rb");
        if (infile == NULL) {
            fprintf(stderr, "Error opening input file \"%s\"\n",
                    argv[optind]);
            return 1;
        }
    }
    if (optind + 1 < argc) {
        outfile = fopen(argv[optind + 1], "wb");
        if (outfile == NULL) {
            fprintf(stderr, "Error opening output file \"%s\"\n",
                    argv[optind + 1]);
            return 1;
        }
    }

    while (fgets(buf, 4096, infile) != NULL) {
	i = strlen(buf);
	if (i > 0 && buf[i-1] == '\n')
	    buf[i-1] = '\0';
	if (!strncmp(buf, "sleep ", 6)) {
	    if (play)
		millisec_sleep(atoi(buf + 6));
	}
	else if (!strncmp(buf, "line ", 5))
	    fprintf(outfile, "%s\n", buf + 5);
	else if (!strncmp(buf, "prompt ", 7))
	    fprintf(outfile, "%s", buf + 7);
	else {
	    fprintf(stderr, "Syntax error in line:\n%s\n", buf);
	    return 1;
	}
	fflush(outfile);
    }
    if (feof(infile)) {
	fprintf(outfile, "\n");
	return 0;
    } else {
	fprintf(stderr, "Error reading file\n");
	return 1;
    }
}

