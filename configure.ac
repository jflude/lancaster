#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.69])
AC_INIT([lancaster], [1.0], [bug-lancaster@peak6.com])
AM_INIT_AUTOMAKE([1.14])

AC_CONFIG_SRCDIR([src/latency.c])
AC_CONFIG_HEADERS([config.h])

# Checks for programs.
AC_PROG_CC
AC_PROG_INSTALL
AC_PROG_RANLIB

# Checks for libraries.
AC_CHECK_LIB([m], [sqrt])
AC_CHECK_LIB([rt], [shm_open])
AC_CHECK_LIB([pthread], [pthread_create])

# Checks for header files.
AC_FUNC_ALLOCA
AC_CHECK_HEADERS([arpa/inet.h fcntl.h float.h limits.h netinet/in.h stddef.h \
			      stdint.h stdlib.h string.h sys/ioctl.h \
			      sys/socket.h sys/time.h unistd.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_TYPE_MODE_T
AC_TYPE_SIZE_T
AC_TYPE_SSIZE_T
AC_CHECK_DECLS([sys_siglist])

# Checks for library functions.
AC_FUNC_MALLOC
AC_FUNC_MMAP
AC_FUNC_REALLOC
AC_CHECK_FUNCS([alarm clock_gettime ftruncate gethostname gettimeofday memset])
AC_CHECK_FUNCS([munmap nanosleep socket sqrt strchr strerror strrchr])

AC_CONFIG_FILES([Makefile])
AC_OUTPUT
