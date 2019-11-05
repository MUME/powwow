# Default help file directory
AM_CPPFLAGS=-D_XOPEN_SOURCE=700 -DPOWWOW_DIR=\"$(pkgdatadir)\" \
	-DPLUGIN_DIR=\"$(plugindir)\"

bin_PROGRAMS = powwow powwow-muc powwow-movieplay
powwow_SOURCES = beam.c cmd.c log.c edit.c cmd2.c eval.c \
		 utils.c main.c tcp.c list.c map.c tty.c \
		 ptr.c
powwow_LDFLAGS = @dl_ldflags@
powwowdir = $(pkgincludedir)
powwow_HEADERS = beam.h cmd.h log.h edit.h cmd2.h eval.h \
		 utils.h main.h tcp.h list.h map.h tty.h \
		 ptr.h defines.h feature/regex.h
powwow_muc_SOURCES = powwow-muc.c
powwow_movieplay_SOURCES = powwow-movieplay.c

install-exec-hook:
	(cd $(DESTDIR)$(bindir) && \
	 rm -f powwow-movie2ascii$(EXEEXT) && \
	 $(LN_S) powwow-movieplay$(EXEEXT) powwow-movie2ascii$(EXEEXT))

noinst_PROGRAMS = catrw follow
follow_SOURCES = follow.c
catrw_SOURCES = catrw.c

EXTRA_DIST = plugtest.c

plugtest.so: plugtest.c
	gcc -shared -o plugtest.so plugtest.c
