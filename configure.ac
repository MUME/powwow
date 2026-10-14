# Process this file with autoconf to produce a configure script.
AC_INIT([powwow],[1.2.24])
AM_INIT_AUTOMAKE([-Wall])

AC_CONFIG_HEADERS([config.h])

AC_CANONICAL_HOST

AC_ARG_ENABLE([vt100],
    AS_HELP_STRING([--enable-vt100],
        [hardcode use of VT100 escape sequences; use if you have no termcap]),
    [],
    [enable_vt100=no])
AS_IF([ test "${enable_vt100}" = yes ],
    [ AC_DEFINE(USE_VT100, 1,
          [Define to hard code use of VT-100 escape sequences.]) ])

AC_ARG_ENABLE([regex],
    AS_HELP_STRING([--enable-regex],
        [enable regular expressions; argument is one of:])
    AS_HELP_STRING([yes], [find first available: pcre2posix, libc (default)])
    AS_HELP_STRING([pcre2posix], [use PCRE2's POSIX API (matches UTF-8 characters)])
    AS_HELP_STRING([libc], [use libc (matches UTF-8 characters only in UTF-8 locales)]),
    [],
    [enable_regex=yes])

AC_ARG_ENABLE([sort],
    AS_HELP_STRING([--enable-sort],[sort aliases and actions]),
    [],
    [enable_sort=no])
AS_IF([ test "${enable_sort}" = yes ],
    [ AC_DEFINE(USE_LIST_SORT, 1, [Enable to sort entries in some lists.]) ])

AC_ARG_ENABLE([shell],
    AS_HELP_STRING([--disable-shell],[disables the "#!" command.]),
    [],
    [enable_shell=yes])
AS_IF([ test "${enable_shell}" != no ],
    [ AC_DEFINE(USE_SHELL, 1, [Define to enable the "#!" command.]) ])

AC_ARG_ENABLE([ansi-bug],
    AS_HELP_STRING([--enable-ansi-bug],
        [enables fixes for "#mark" ansi bugs on some terminals]),
    [],
    [enable_ansi_bug=no])
AS_IF([ test "${enable_ansi_bug}" != no ],
    [ AC_DEFINE(USE_ANSIBUG, 1,
          [Enable fix for "#mark" ANSI bugs on some terminals.]) ])

is_darwin=
case "${host_os}" in
    darwin*) is_darwin=t ;;
esac

AC_ARG_ENABLE([bsd],
    AS_HELP_STRING([--enable-bsd], [enable BSD features (default on Darwin)]),
    [],
    [ enable_bsd=no; test -n "${is_darwin}" && enable_bsd=yes ])

AS_IF([ test "${enable_bsd}" = yes ],
    [ AC_DEFINE(USE_SGTTY, 1, [Define to use BSD sgtty.])
      AC_DEFINE(USE_BSD_LIKE, 1,
    [Define for building on BSD-like systems.]) ])

AC_ARG_ENABLE(tls,
    AS_HELP_STRING([--disable-tls],[disables TLS support]),
    [],
    [enable_tls=yes])

AC_ARG_WITH([libgrapheme],
    AS_HELP_STRING([--without-libgrapheme],
        [do not use libgrapheme to find grapheme clusters (emoji
         sequences, flags, ...); default: use it if available]),
    [],
    [with_libgrapheme=check])

AS_IF([ test "${enable_tls}" != no ],
    [ AC_DEFINE(USE_TLS, 1, [Define to enable TLS using GnuTLS.]) ])

# Checks for programs.
AC_PROG_CC
AC_MSG_CHECKING([C version])
case "x${ac_prog_cc_stdc}" in
    xc89 ) AC_MSG_ERROR([C99 or later is required; found C89]) ;;
    xc99 ) AC_MSG_RESULT(C99) ;;
    xc11 ) AC_MSG_RESULT(C11) ;;
    xc23 ) AC_MSG_RESULT(C23) ;;
    x ) AC_MSG_ERROR([not found]) ;;
    * ) AC_MSG_WARN([unknown version ${ac_prog_cc_stdc}]) ;;
esac
AC_PROG_LN_S

# Checks for libraries.
AC_CHECK_FUNC([lrand48], ,
    AC_DEFINE(USE_RANDOM, 1, [Define to use random() rather than lrand48().]))

if test "x${enable_vt100}" != "xyes"; then
    AC_SEARCH_LIBS(initscr,[ncurses curses], [], [
        if test "x${enable_vt100}" = "xno" ; then
            AC_MSG_ERROR([*** curses libraries not found])
        fi])
fi

AC_CHECK_FUNC([getopt_long],
    AC_DEFINE(HAVE_GETOPT_LONG, 1, [Define to use getopt_long().]))

# Dynamic modules
AC_SEARCH_LIBS(dlopen,[dl],[
    AC_DEFINE(HAVE_LIBDL, 1, [Define if we have -ldl.])
    dl_ldflags="-rdynamic"])
AC_SUBST(dl_ldflags)

AS_IF([test "x${enable_tls}" = xyes], [
    AC_SEARCH_LIBS(gnutls_global_init, gnutls, [
        AC_DEFINE([HAVE_GNUTLS], 1,
            [Define to 1 to use the 'gnutls' library.]) ],
        AC_MSG_ERROR([cannot find gnutls]))
    AC_CHECK_HEADER(gnutls/gnutls.h, ,
        AC_MSG_ERROR([cannot find gnutls/gnutls.h])) ])

AS_IF([test "x${with_libgrapheme}" != xno], [
    have_libgrapheme=no
    AC_CHECK_HEADER(grapheme.h,
        [AC_SEARCH_LIBS(grapheme_next_character_break_utf8, grapheme,
            [have_libgrapheme=yes])])
    AS_IF([test "x${have_libgrapheme}" = xyes],
        [with_libgrapheme=yes
         AC_DEFINE([HAVE_LIBGRAPHEME], 1,
            [Define to 1 to use libgrapheme for grapheme clusters.])],
        [test "x${with_libgrapheme}" = xcheck || \
            AC_MSG_ERROR([cannot find libgrapheme])
         with_libgrapheme=no]) ])

# Checks for header files.
AC_CHECK_HEADER([locale.h],
    [ AC_CHECK_FUNC([putwc],
          [ AC_DEFINE(USE_LOCALE, 1, [
                Enable locale support for wide characters.])])])

if test "x${enable_regex}" = "xno"; then
    AC_MSG_RESULT([Regex support disabled])
    enable_regex_using="disabled"
else
    enable_regex_using="not_found"

    if test "x${enable_regex}" = "xyes"; then
        AC_CHECK_HEADER("pcre2posix.h",[
            enable_regex_using="pcre2posix"
        ], [
            AC_CHECK_HEADER("regex.h",[
                enable_regex_using="libc"
            ])
        ])
    else
        # Use the options defined
        enable_regex_using="${enable_regex}"
    fi

    if test "x${enable_regex_using}" = "xnot_found"; then
        # no regex
        enable_regex=no
        AC_MSG_RESULT([*** Unable to enable regex support])
    else
        AC_DEFINE(USE_REGEXP, 1, [Define to enable regular expression support.])

        if test "x${enable_regex_using}" = "xpcre2posix"; then
            AC_MSG_RESULT([Using pcre2posix regcomp])
            AC_DEFINE(USE_REGEXP_PCRE2POSIX, 1, [Define to use pcre2posix.])
            dnl since PCRE2 10.33, pcre2posix.h renames regcomp to pcre2_regcomp
            AC_CHECK_LIB(pcre2-posix, pcre2_regcomp,
                [LIBS="-lpcre2-posix $LIBS"],
                [AC_CHECK_LIB(pcre2-posix, regcomp,
                    [LIBS="-lpcre2-posix $LIBS"],
                    AC_MSG_ERROR([*** pcre2-posix library not found]))])
        elif test "x${enable_regex_using}" = "xlibc"; then
            AC_MSG_RESULT([Using libc regcomp])
            AC_CHECK_FUNC(regcomp,,
                AC_MSG_ERROR([*** regcomp not found in libc])
            )
        else
            AC_MSG_ERROR([*** unknown regex option: $enable_regex_using])
        fi
    fi
fi

AC_ARG_WITH([plugindir],
    AS_HELP_STRING([--with-plugindir=DIR],
        [Plugin installation directory; default=LIBDIR/powwow]),
    [plugindir="${withval}"],
    [plugindir="\${libdir}/powwow"])

AC_SUBST(plugindir)

AC_CONFIG_FILES([Makefile src/Makefile doc/powwow.doc doc/Makefile
    man/powwow.6 man/Makefile])
AC_OUTPUT

cat <<EOF


Powwow has been configured with the following options:

Version:            ${VERSION}
User binaries:      $(eval eval eval echo "${bindir}")
Data directory:     $(eval eval eval echo "${datadir}/${PACKAGE}")
Plugin directory:   $(eval eval eval echo "${plugindir}")

enable-vt100:       ${enable_vt100}
enable-regex:       ${enable_regex} (${enable_regex_using})
enable-tls:         ${enable_tls}
with-libgrapheme:   ${with_libgrapheme}
enable-sort:        ${enable_sort}
enable-shell:       ${enable_shell}
enable-ansi-bug:    ${enable_ansi_bug}
enable-bsd:         ${enable_bsd}

Host:               ${host}
Compiler:           ${CC}
Preprocessor flags: ${CPPFLAGS}
Compiler flags:     ${CFLAGS}
Linker flags:       ${LDFLAGS}
Libraries:          ${LIBS}

EOF
