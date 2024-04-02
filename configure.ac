#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

# Autoconf requirements.
AC_PREREQ([2.69])

# Information on the package.
AC_INIT([lancaster], [1.0], [bug-lancaster@peak6.com])
AM_INIT_AUTOMAKE([-Wall -Werror foreign])
AC_CONFIG_MACRO_DIRS([m4])
AC_CONFIG_SRCDIR([src/error.c])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_FILES([Makefile])

# Checks for programs.
AC_PROG_CC
AC_PROG_INSTALL
AM_PROG_AR
LT_INIT

# Checks for libraries.
AC_CHECK_LIB([m], [sqrt])
AC_CHECK_LIB([rt], [shm_open])
AC_CHECK_LIB([pthread], [pthread_create])

# Checks for header files.
AC_CHECK_HEADERS([arpa/inet.h fcntl.h float.h inttypes.h limits.h malloc.h])
AC_CHECK_HEADERS([netinet/in.h stddef.h stdlib.h string.h sys/ioctl.h])
AC_CHECK_HEADERS([sys/socket.h sys/time.h unistd.h])

# Checks for types.
AC_TYPE_INT64_T
AC_TYPE_SIZE_T
AC_TYPE_SSIZE_T
AC_TYPE_MODE_T
AC_CHECK_DECLS([sys_siglist])

# Checks for structures.

# Checks for compiler characteristics.
AC_C_BIGENDIAN
AC_C_SYNC_INTRINSICS

# Checks for library functions.
AC_FUNC_ALLOCA
AC_FUNC_MMAP
AC_FUNC_STRERROR_R
AC_CHECK_FUNCS([alarm clock_gettime ftruncate gethostbyname gethostname])
AC_CHECK_FUNCS([gettimeofday lldiv memset msync munmap nanosleep socket])
AC_CHECK_FUNCS([sqrt strchr strrchr strsignal])

# Checks for system services.
AC_SYS_OS_CPU_TYPES
AC_OUTPUT
