AC_PREREQ(2.61)
dnl numbering example: 4.6.test1 4.6.test2 4.6.3 4.6.patch4 4.6.patch5
AC_INIT([CLEX File Manager],[4.7],[https://github.com/xitop/clex/issues],[clex])
AC_SUBST([RPMRELEASE],1)

AM_INIT_AUTOMAKE
AC_CONFIG_SRCDIR([src/clex.h])
AC_CONFIG_HEADER([config.h])

# Checks for programs.
AC_PROG_CC
if test "$GCC" = "yes" ; then
	CFLAGS="$CFLAGS -Wall -pedantic"
	if gcc --help=warnings 2>&1 | grep -q 'Wstrict-overflow=' ; then
		CFLAGS="$CFLAGS -Wstrict-overflow=0"
	fi
	if gcc --help=warnings 2>&1 | grep -q 'Wformat-overflow=' ; then
		CFLAGS="$CFLAGS -Wformat-overflow=0"
	fi
fi

# Checks for libraries.
AC_SEARCH_LIBS([setupterm],[tinfo])
AC_SEARCH_LIBS([addwstr],[ncursesw ncurses cursesw curses],[CURSESLIB="yes"],[CURSESLIB="no"])
if test "$CURSESLIB" = "no" ; then
	AC_MSG_ERROR([CLEX requires CURSES library with a wide character support])
fi

# Checks for header files.

dnl  glibc 2.25 deprecates 'major' and 'minor' in <sys/types.h> and requires to
dnl  include <sys/sysmacros.h>. However the logic in AC_HEADER_MAJOR has not yet
dnl  been updated in Autoconf 2.69
if test "$GCC" = "yes" ; then
	saved_CFLAGS=$CFLAGS
	CFLAGS="$CFLAGS -Werror"
	AC_HEADER_MAJOR
	CFLAGS=$saved_CFLAGS
else
	AC_HEADER_MAJOR
fi

AC_CHECK_HEADERS([fcntl.h langinfo.h limits.h locale.h stdlib.h string.h termios.h unistd.h wchar.h wctype.h])
if echo "$LIBS" | grep -e "-lncurses" > /dev/null ; then
	dnl ncurses header file for ncurses library
	for dir in /usr/include /opt/include /usr/local/include /opt/local/include ; do
		for subdir in ncursesw ncurses ; do
			if test -d "$dir/$subdir" ; then
				CPPFLAGS="$CPPFLAGS -I$dir/$subdir"
			fi
		done
	done
	AC_CHECK_HEADERS([ncursesw.h ncurses.h])
else
	dnl curses header file for curses library
	for dir in /usr/include /opt/include /usr/local/include /opt/local/include ; do
		for subdir in cursesw curses ; do
			if test -d "$dir/$subdir" ; then
				CPPFLAGS="$CPPFLAGS -I$dir/$subdir"
			fi
		done
	done
	AC_CHECK_HEADERS([cursesw.h curses.h])
fi
AC_CHECK_HEADERS([term.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_TYPE_UID_T
AC_TYPE_MODE_T
AC_TYPE_OFF_T
AC_TYPE_PID_T
AC_TYPE_SIZE_T
AC_TYPE_SSIZE_T
AC_TYPE_SIGNAL
AC_CHECK_MEMBERS([struct stat.st_rdev])
AC_DECL_SYS_SIGLIST
AC_FUNC_FNMATCH
AC_FUNC_FORK
AC_FUNC_STRCOLL

# Checks for library functions.
AC_DEFINE([_GNU_SOURCE],[1],[required for strsignal])
AC_CHECK_FUNCS([alarm btowc dup2 endgrent endpwent getcwd iswprint memset nl_langinfo setenv setlocale strchr strerror strsignal uname wcwidth])

# Checks for system services.
AC_SYS_LARGEFILE

AC_CONFIG_FILES([Makefile clex.spec src/Makefile])
AC_OUTPUT
