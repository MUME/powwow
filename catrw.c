/*
 *  catrw.c  --  open a file with O_RDWR and print it.
 * 
 *  This file is placed in the public domain.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#define BUFSIZE 4096
char buf[BUFSIZE];

void catrw(int fd) {
    int i;
    for (;;) {
	while ( (i = read(fd, buf, BUFSIZE)) < 0 && errno == EINTR)
	    ;
	if (i <= 0)
	    break;
	write(1, buf, i);
    }
}

int main(int argc, char *argv[]) {
    int fd;
    if (argc == 1)
	catrw(0);
    else {
	while (--argc && (fd = open(*++argv, O_RDWR))) {
	    catrw(fd);
	    close(fd);
	}
    }
    return 0;
}

