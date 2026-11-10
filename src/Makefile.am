# Default help file directory
AM_CPPFLAGS=-D_XOPEN_SOURCE=700 @feature_cppflags@ -DPOWWOW_DIR=\"$(pkgdatadir)\" \
	-DPLUGIN_DIR=\"$(plugindir)\" -DPOWWOW_BUILD_DIR=\"$(abs_builddir)\" \
	-DPOWWOW_SOURCE_DIR=\"$(abs_top_srcdir)\"
AM_CFLAGS = $(WARN_CFLAGS)

bin_PROGRAMS = powwow powwow-movieplay
if HAVE_CURSES
bin_PROGRAMS += powwow-muc
endif
powwow_SOURCES = beam.c cmd.c log.c edit.c cmd2.c eval.c \
		 utils.c main.c tcp.c list.c map.c tty.c \
		 ptr.c tls.c charset.c width.c sgr.c attr.c charname.c
powwow_LDFLAGS = @dl_ldflags@
powwowdir = $(pkgincludedir)
powwow_HEADERS = beam.h cmd.h log.h edit.h cmd2.h eval.h \
		 utils.h main.h tcp.h list.h map.h tty.h \
		 ptr.h defines.h tls.h charset.h width.h sgr.h attr.h \
		 charname.h \
		 feature/regex.h
powwow_muc_SOURCES = powwow-muc.c
powwow_muc_LDADD = @CURSES_LIBS@
powwow_movieplay_SOURCES = powwow-movieplay.c

noinst_PROGRAMS = follow
follow_SOURCES = follow.c

check_PROGRAMS = test_charset test_ptr test_sgr test_attr
test_charset_SOURCES = test_charset.c charset.c width.c
test_ptr_SOURCES = test_ptr.c ptr.c
test_sgr_SOURCES = test_sgr.c sgr.c
test_attr_SOURCES = test_attr.c attr.c
TESTS = test_charset test_ptr test_sgr test_attr test_commands.sh

EXTRA_DIST = plugtest.c test_commands.sh

plugtest.so: plugtest.c
	$(CC) -c -fPIC $(DEFS) $(DEFAULT_INCLUDES) $(AM_CPPFLAGS) \
		$(CPPFLAGS) $(WARN_CFLAGS) $(CFLAGS) -o plugtest.o $(srcdir)/plugtest.c
	$(CC) -shared $(CFLAGS) $(plugin_ldflags) $(LDFLAGS) \
		-o plugtest.so plugtest.o

CLEANFILES = plugtest.so

SUFFIXES = .tidy
.c.tidy:
	$(AM_V_at)$(CLANG_TIDY) --quiet --config-file=$(top_srcdir)/.clang-tidy \
		$(CLANG_TIDY_FLAGS) $< \
		-- $(DEFS) $(DEFAULT_INCLUDES) $(AM_CPPFLAGS) $(CPPFLAGS)

# "make -j8 tidy" checks everything, "make foo.tidy" checks only foo.c
tidy: $(SOURCES:.c=.tidy) plugtest.tidy

.PHONY: tidy
