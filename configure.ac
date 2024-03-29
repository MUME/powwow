# Process this file with autoconf to produce a configure script.
AC_INIT(powwow, 1.2.23)
AM_INIT_AUTOMAKE

AC_CANONICAL_HOST

AC_ARG_ENABLE(vt100,
	AC_HELP_STRING([--enable-vt100],
			[Hard code VT100 escape sequences; use if you have no termcap [[default=no]]]),
        ,
        [enable_vt100="no"]
)
AS_IF([ test "${enable_vt100}" = yes ],
      [ AC_DEFINE(USE_VT100) ])

AC_ARG_ENABLE(regex,

        AS_HELP_STRING([--enable-regex],
            [Enable regular expressions [[default=yes]]])
        []
        [yes            - find first available: pcreposix, libc]
        [pcreposix      - use pcreposix]
        [libc           - use libc]
        ,,
        [enable_regex="yes"]
)

AC_ARG_ENABLE(sort,
	AC_HELP_STRING([--enable-sort],
			[Sort aliases and actions [[default=no]]]),
        ,
        [enable_sort="no"]
)
AS_IF([ test "${enable_sort}" = yes ],
      [ AC_DEFINE(DO_SORT) ])

AC_ARG_ENABLE(noshell,
	AC_HELP_STRING([--enable-noshell],
			[Disables the "#!" command [[default=no]]]),
        ,
        [enable_noshell="no"]
)
AS_IF([ test "${enable_noshell}" = yes ],
      [ AC_DEFINE(NO_SHELL) ])

AC_ARG_ENABLE(ansibug,
	AC_HELP_STRING([--enable-ansibug],
		       [Enables fixes for "#mark" ansi bugs on some terminals [[default=no]]]),
        ,
        [enable_ansibug="no"]
)
AS_IF([ test "${enable_ansibug}" = yes ],
      [ AC_DEFINE(BUG_ANSI) ])

AC_ARG_ENABLE(bsd,
	AC_HELP_STRING([--enable-bsd],
		       [Needed for BSD systems, in powwow this was USE_SGTTY and BSD_LIKE. Default on Darwin (OS X) systems.]))
AS_IF([ test -z "${enable_bsd}" ],
      [ case "${host_os}" in
            darwin*) enable_bsd=yes ;;
            *)       enable_bsd=no ;;
        esac])
AS_IF([ test "${enable_bsd}" = yes ],
      [ AC_DEFINE(USE_SGTTY)
        AC_DEFINE(BSD_LIKE) ])

AC_MSG_CHECKING([man page encoding])
if test -z "${MAN_PAGE_ENCODING}"; then
   case "${host_os}" in
       darwin*) MAN_PAGE_ENCODING=ISO-8859-1 ;;
       *)       MAN_PAGE_ENCODING=UTF-8 ;;
   esac
fi
AC_SUBST([MAN_PAGE_ENCODING])
AC_MSG_RESULT([${MAN_PAGE_ENCODING}])

if test "${MAN_PAGE_ENCODING}" != UTF-8; then
   AC_CHECK_PROG([ICONV], [iconv], [iconv], [])
   if test -z "${ICONV}" -a "${MAN_PAGE_ENCODING}" != UTF-8; then
      AC_MSG_WARN([Forcing man page encoding to UTF-8])
      MAN_PAGE_ENCODING=UTF-8
   fi
fi

AM_CONDITIONAL([MAN_PAGE_ENCODING_IS_UTF_8],
               [test "${MAN_PAGE_ENCODING}" = UTF-8 ])

# Checks for programs.
AC_PROG_CC_C89
AC_PROG_LN_S

# Checks for libraries.
AC_CHECK_FUNC(lrand48,,AC_DEFINE(USE_RANDOM))

if test "x${enable_vt100}" != "xyes"; then
    AC_SEARCH_LIBS(initscr,[ncurses curses], [], [
                   if test "x${enable_vt100}" = "xno" ; then
                       AC_MSG_ERROR([*** curses libraries not found])
                   fi])
fi

# Dynamic modules
AC_SEARCH_LIBS(dlopen,[dl],[
               AC_DEFINE(HAVE_LIBDL)
               dl_ldflags="-rdynamic"])
AC_SUBST(dl_ldflags)

# Checks for header files.
AC_CHECK_HEADERS([stdlib.h unistd.h])
AC_CHECK_HEADER([locale.h],
    [AC_CHECK_FUNC([putwc],[AC_DEFINE(USE_LOCALE)])])

if test "x${enable_regex}" = "xno"; then
    AC_MSG_RESULT([Regex support disabled])
    enable_regex_using="disabled"
else
    enable_regex_using="not_found"

    if test "x${enable_regex}" = "xyes"; then
        AC_CHECK_HEADER("pcreposix.h",[
            enable_regex_using="pcreposix"
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
        AC_DEFINE(USE_REGEXP)

        if test "x${enable_regex_using}" = "xpcreposix"; then
            AC_MSG_RESULT([Using pcreposix regcomp])
            AC_DEFINE(USE_REGEXP_PCREPOSIX)
            AC_CHECK_LIB(pcreposix,regcomp,,
                AC_MSG_ERROR([*** pcreposix library not found])
            )
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
            AC_HELP_STRING([--with-plugindir=DIR],
                           [Plugin installation directory [[default=LIBDIR/powwow]]])],
            [plugindir="${withval}"],
            [plugindir="\${libdir}/powwow"])

AC_SUBST(plugindir)

# Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST

# Checks for library functions.
AC_FUNC_MALLOC

AC_OUTPUT(Makefile src/Makefile doc/powwow.doc doc/Makefile man/powwow.6.utf-8 man/Makefile)

cat <<EOF


Powwow has been configured with the following options:

Version:            ${VERSION}
User binaries:      $(eval eval eval echo "${bindir}")
Data directory:     $(eval eval eval echo "${datadir}/${PACKAGE}")
Plugin directory:   $(eval eval eval echo "${plugindir}")

enable-vt100:       ${enable_vt100}
enable-regex:       ${enable_regex} (${enable_regex_using})
enable-sort:        ${enable_sort}
enable-noshell:     ${enable_noshell}
enable-ansibug:     ${enable_ansibug}
enable-bsd:         ${enable_bsd}

Man page encoding:  ${MAN_PAGE_ENCODING}

Host:               ${host}
Compiler:           ${CC}
Preprocessor flags: ${CPPFLAGS}
Compiler flags:     ${CFLAGS}
Linker flags:       ${LDFLAGS}
Libraries:          ${LIBS}

EOF
