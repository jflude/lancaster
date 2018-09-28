#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.69])
AC_INIT([lancaster], [1.0], [bug-lancaster@peak6.com])
AM_INIT_AUTOMAKE([1.14])

AC_CONFIG_SRCDIR([src/error.c])
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
AC_CHECK_HEADERS([arpa/inet.h fcntl.h float.h inttypes.h limits.h malloc.h])
AC_CHECK_HEADERS([netinet/in.h stddef.h stdlib.h string.h sys/ioctl.h])
AC_CHECK_HEADERS([sys/socket.h sys/time.h unistd.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_TYPE_INT64_T
AC_TYPE_SIZE_T
AC_TYPE_SSIZE_T
AC_TYPE_MODE_T
AC_CHECK_DECLS([sys_siglist])

# Checks for library functions.
AC_FUNC_ALLOCA
AC_FUNC_MMAP
AC_FUNC_STRERROR_R
AC_CHECK_FUNCS([alarm clock_gettime ftruncate gethostname gettimeofday])
AC_CHECK_FUNCS([memset msync munmap nanosleep socket sqrt strchr strrchr])

# Checks for the host's type of CPU and operating system.
AC_CANONICAL_HOST
AS_CASE([$host],
[*-*-cygwin*], AC_DEFINE([CYGWIN_OS], [1], [Host operating system is Cygwin]),
[*-*-darwin*], AC_DEFINE([DARWIN_OS], [1], [Host operating system is Darwin]))
AS_CASE([$host],
[x86_64*], AC_DEFINE([X86_64_CPU], [1], [Host CPU is x86_64]),
[arm*], AC_DEFINE([ARM_CPU], [1], [Host CPU is ARM]),
[mips*], AC_DEFINE([MIPS_CPU], [1], [Host CPU is MIPS]),
[ppc*], AC_DEFINE([PPC_CPU], [1], [Host CPU is PPC]))

AC_CONFIG_FILES([Makefile])
AC_OUTPUT
