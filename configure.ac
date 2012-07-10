#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.69])
AC_INIT(fec, 0.12, aleksi.norman@uta.fi)
AM_INIT_AUTOMAKE(fec, 0.12)
AC_OUTPUT(Makefile)

AC_CONFIG_SRCDIR([fec.cpp])
AC_CONFIG_HEADERS([config.h])

# Checks for programs.
AC_PROG_CXX

# Checks for libraries.

# Checks for header files.
AC_CHECK_HEADERS([unistd.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_CHECK_HEADER_STDBOOL
AC_TYPE_OFF_T

# Checks for library functions.
AC_FUNC_LSTAT_FOLLOWS_SLASHED_SYMLINK
AC_CHECK_FUNCS([strcasecmp strdup strrchr lstat])
AC_CHECK_FUNCS([malloc memcpy strlen stat], [], AC_MSG_ERROR([prerequisite not met]) )

AC_OUTPUT
