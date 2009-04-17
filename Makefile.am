# Default help file directory
AM_CFLAGS=-DPOWWOW_DIR=\"$(pkgdatadir)\" -DPLUGIN_DIR=\"$(plugindir)\"

bin_PROGRAMS = powwow muc powwow-movieplay
powwow_SOURCES = beam.c cmd.c log.c edit.c cmd2.c eval.c \
		 utils.c main.c tcp.c list.c map.c tty.c \
		 ptr.c
powwow_LDFLAGS = @dl_ldflags@
powwowdir = $(pkgincludedir)
powwow_HEADERS = beam.h cmd.h log.h edit.h cmd2.h eval.h \
		 utils.h main.h tcp.h list.h map.h tty.h \
		 ptr.h defines.h
muc_SOURCES = muc.c
powwow_movieplay_SOURCES = powwow-movieplay.c

noinst_PROGRAMS = catrw follow
follow_SOURCES = follow.c
catrw_SOURCES = catrw.c

man_MANS = powwow.6
pkgdata_DATA = powwow.doc powwow.help COPYING
EXTRA_DIST = README README.follow README.term README.modules \
		TODO Hacking Config.demo \
		powwow.doc powwow.help powwow.6 plugtest.c debian/rules \
		debian/control debian/changelog powwow.6.utf-8

CLEANFILES = powwow.6

powwow.6: powwow.6.utf-8
if MAN_PAGE_ENCODING_IS_UTF_8
	cp $< $@
else
	iconv -f UTF-8 -t "$(MAN_PAGE_ENCODING)" < $< > $@
endif

plugtest.so: plugtest.c
	gcc -shared -o plugtest.so plugtest.c
