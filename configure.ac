# Process this file with autoconf to produce a configure script.
AC_INIT([powwow],[1.3.2-dev])
AM_INIT_AUTOMAKE([foreign -Wall])

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

AC_ARG_ENABLE([shell],
    AS_HELP_STRING([--disable-shell],[disables the "#!" command.]),
    [],
    [enable_shell=yes])
AS_IF([ test "${enable_shell}" != no ],
    [ AC_DEFINE(USE_SHELL, 1, [Define to enable the "#!" command.]) ])

# _XOPEN_SOURCE hides extensions such as SIGWINCH; make these systems
# show them
is_darwin=
is_cygwin=
feature_cppflags=
case "${host_os}" in
    cygwin*) is_cygwin=t ;;
    darwin*) is_darwin=t; feature_cppflags=-D_DARWIN_C_SOURCE ;;
    freebsd* | dragonfly*) feature_cppflags=-D__BSD_VISIBLE=1 ;;
    openbsd*) feature_cppflags=-D_BSD_SOURCE ;;
    netbsd*) feature_cppflags=-D_NETBSD_SOURCE ;;
esac
AC_SUBST(feature_cppflags)
# make help lists the cygwin targets only on Cygwin
AM_CONDITIONAL([CYGWIN], [test -n "${is_cygwin}"])

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

AC_ARG_WITH([zlib],
    AS_HELP_STRING([--without-zlib],
        [do not use zlib, to read the names of the characters and emoji
         installed compressed; default: use it if available]),
    [],
    [with_zlib=check])

AS_IF([ test "${enable_tls}" != no ],
    [ AC_DEFINE(USE_TLS, 1, [Define to enable TLS using GnuTLS.]) ])

# Checks for programs.
AC_PROG_CC
AC_MSG_CHECKING([C version])
case "x${ac_prog_cc_stdc}" in
    xc89 ) AC_MSG_ERROR([C11 or later is required; found C89]) ;;
    xc99 ) AC_MSG_ERROR([C11 or later is required; found C99]) ;;
    xc11 ) AC_MSG_RESULT(C11) ;;
    xc23 ) AC_MSG_RESULT(C23) ;;
    x ) AC_MSG_ERROR([not found]) ;;
    * ) AC_MSG_WARN([unknown version ${ac_prog_cc_stdc}]) ;;
esac

# the warnings that the code is kept free of, those that the compiler
# accepts; a -Wno-... is checked as the warning it turns off, as gcc
# accepts any -Wno-...
powwow_warnings="all extra missing-declarations missing-prototypes \
    nested-externs no-missing-field-initializers no-unused-parameter \
    old-style-definition pointer-arith shadow unused-macros write-strings"
AS_IF([${CC} --version 2>/dev/null | grep clang >/dev/null],
    [powwow_warnings="${powwow_warnings} assign-enum embedded-directive \
        extra-semi extra-semi-stmt gnu-union-cast gnu-empty-initializer \
        gnu-empty-struct gnu-folding-constant \
        gnu-conditional-omitted-operand gnu-flexible-array-initializer \
        missing-noreturn missing-variable-declarations \
        tautological-unsigned-zero-compare"],
    [powwow_warnings="${powwow_warnings} no-packed-bitfield-compat"])
WARN_CFLAGS=
echo 'int main(void) { return 0; }' > conftest-warn.c
for warning in ${powwow_warnings}; do
    probe=`echo "${warning}" | sed 's/^no-//'`
    AC_MSG_CHECKING([whether ${CC} accepts -W${warning}])
    AS_IF([${CC} ${CFLAGS} -Werror -W${probe} -c conftest-warn.c \
               -o conftest-warn.o >/dev/null 2>&1],
        [AC_MSG_RESULT([yes])
         WARN_CFLAGS="${WARN_CFLAGS} -W${warning}"],
        [AC_MSG_RESULT([no])])
done
rm -f conftest-warn.c conftest-warn.o
AC_SUBST([WARN_CFLAGS])
# clang-tidy, or else the newest with a version, as clang-tidy-15 (Debian,
# Ubuntu) or clang-tidy19 (FreeBSD), with one line of output
AC_CACHE_CHECK([for clang-tidy], [ac_cv_path_CLANG_TIDY],
    [AC_PATH_PROGS_FEATURE_CHECK([CLANG_TIDY],
        [clang-tidy m4_for([v], [30], [15], [-1],
                           [[clang-tidy-]v[ clang-tidy]v ])],
        [ac_cv_path_CLANG_TIDY=$ac_path_CLANG_TIDY
         ac_path_CLANG_TIDY_found=:],
        [ac_cv_path_CLANG_TIDY=clang-tidy],
        [$PATH:/opt/homebrew/opt/llvm/bin:/usr/local/opt/llvm/bin])])
AC_SUBST([CLANG_TIDY], [$ac_cv_path_CLANG_TIDY])

# Checks for libraries.

# powwow uses curses for termcap, unless --enable-vt100; powwow-muc always
# uses curses, and is not built without it
save_LIBS=$LIBS
AC_SEARCH_LIBS([initscr], [ncurses curses], [have_curses=yes],
    [have_curses=no])
AS_IF([test "x${enable_vt100}" = xyes], [
    AS_IF([test "x${have_curses}" = xyes &&
           test "x${ac_cv_search_initscr}" != "xnone required"],
        [CURSES_LIBS=${ac_cv_search_initscr}])
    LIBS=$save_LIBS
], [test "x${have_curses}" = xno], [
    AC_MSG_ERROR([*** curses libraries not found])
])
AC_SUBST([CURSES_LIBS])
AM_CONDITIONAL([HAVE_CURSES], [test "x${have_curses}" = xyes])

AC_CHECK_FUNC([getopt_long],
    AC_DEFINE(HAVE_GETOPT_LONG, 1, [Define to use getopt_long().]))

# Dynamic modules, except on Cygwin, where a DLL cannot use the symbols
# of the powwow executable without an import library
AS_IF([test -z "${is_cygwin}"], [
    AC_SEARCH_LIBS(dlopen,[dl],[
        AC_DEFINE(HAVE_LIBDL, 1, [Define if we have -ldl.])
        dl_ldflags="-rdynamic"]) ])
AC_SUBST(dl_ldflags)

# plugins use symbols from the powwow binary, which macOS must allow
plugin_ldflags=
test -n "${is_darwin}" && plugin_ldflags="-Wl,-undefined,dynamic_lookup"
AC_SUBST(plugin_ldflags)

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

AS_IF([test "x${with_zlib}" != xno], [
    have_zlib=no
    AC_CHECK_HEADER(zlib.h,
        [AC_SEARCH_LIBS(gzopen, z, [have_zlib=yes])])
    AS_IF([test "x${have_zlib}" = xyes],
        [with_zlib=yes
         AC_DEFINE([HAVE_ZLIB], 1,
            [Define to 1 to read the files of names compressed by gzip.])],
        [test "x${with_zlib}" = xcheck || AC_MSG_ERROR([cannot find zlib])
         with_zlib=no]) ])
AM_CONDITIONAL([HAVE_ZLIB], [test "x${with_zlib}" = xyes])

# Checks for header files.
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

AC_CONFIG_FILES([Makefile src/Makefile doc/powwow.md doc/Makefile
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
with-zlib:          ${with_zlib}
enable-shell:       ${enable_shell}

Host:               ${host}
Compiler:           ${CC}
Preprocessor flags: ${CPPFLAGS}
Compiler flags:     ${CFLAGS}
Warning flags:      ${WARN_CFLAGS}
Linker flags:       ${LDFLAGS}
Libraries:          ${LIBS}

EOF
