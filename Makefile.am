# Default help file directory
AM_CFLAGS=-DPOWWOW_DIR=\"$(pkgdatadir)\"

bin_PROGRAMS = powwow muc catrw follow movie
powwow_SOURCES = beam.c cmd.c log.c edit.c cmd2.c eval.c \
		 utils.c main.c tcp.c list.c map.c tty.c \
		 ptr.c
powwow_LDFLAGS = @dl_ldflags@
powwowdir = $(pkgincludedir)
powwow_HEADERS = beam.h cmd.h log.h edit.h cmd2.h eval.h \
		 utils.h main.h tcp.h list.h map.h tty.h \
		 ptr.h defines.h
muc_SOURCES = muc.c
movie_SOURCES = movie.c
follow_SOURCES = follow.c
catrw_SOURCES = catrw.c

man_MANS = powwow.6
pkgdata_DATA = powwow.doc powwow.help
EXTRA_DIST = README README.follow README.term README.modules \
		TODO Hacking Config.demo \
		powwow.doc powwow.help powwow.6 plugtest.c debian/rules \
		debian/control debian/changelog

plugtest.so: plugtest.c
	gcc -shared -o plugtest.so plugtest.c
