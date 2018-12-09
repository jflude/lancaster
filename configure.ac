#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

# Autoconf requirements.
AC_PREREQ([2.69])

# Information on the package.
AC_INIT([lancaster], [1.0], [bug-lancaster@peak6.com])
AM_INIT_AUTOMAKE([-Wall -Werror foreign])

# Checks for programs.
AC_PROG_CC
AC_PROG_INSTALL
AC_PROG_RANLIB
AM_PROG_AR

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
AC_CANONICAL_HOST
AC_CACHE_CHECK([for compiler synchronization intrinsics],
	       [ac_cv_sync_intrinsics],
[if test -n "${with_target_subdir}"; then
     case "$host" in
     hppa*-*-hpux*) ac_cv_sync_intrinsics=no ;;
     *) ac_cv_sync_intrinsics=yes ;;
     esac
 else
     AC_LINK_IFELSE(
       [AC_LANG_PROGRAM([int i;],
                        [__sync_fetch_and_or(&i, i);
                         __sync_lock_release(&i);
                         __sync_synchronize();])],
       [ac_cv_sync_intrinsics=yes],
       [ac_cv_sync_intrinsics=no])
 fi])
if test "$ac_cv_sync_intrinsics" = "yes"; then
    AC_DEFINE([HAVE_SYNC_INTRINSICS], 1,
	      [Define to 1 if your compiler has synchronization intrinsics])
fi

# Checks for library functions.
AC_FUNC_ALLOCA
AC_FUNC_MMAP
AC_FUNC_STRERROR_R
AC_CHECK_FUNCS([alarm clock_gettime ftruncate gethostname gettimeofday])
AC_CHECK_FUNCS([memset msync munmap nanosleep socket sqrt strchr strrchr])

# Checks for system services.
AS_CASE([$host],
[x86_64*], AC_DEFINE([X86_64_CPU], [1], [Host CPU is x86_64]),
[arm*], AC_DEFINE([ARM_CPU], [1], [Host CPU is ARM]),
[mips*], AC_DEFINE([MIPS_CPU], [1], [Host CPU is MIPS]),
[ppc*], AC_DEFINE([PPC_CPU], [1], [Host CPU is PPC]))
AS_CASE([$host],
[*-*-cygwin*], AC_DEFINE([CYGWIN_OS], [1], [Host operating system is Cygwin]),
[*-*-darwin*], AC_DEFINE([DARWIN_OS], [1], [Host operating system is Darwin]))

AC_CONFIG_SRCDIR([src/error.c])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_FILES([Makefile])
AC_OUTPUT
