AC_DEFUN([AC_C_SYNC_INTRINSICS], [
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
    AC_DEFINE([HAVE_SYNC_INTRINSICS], [1],
	      [Define to 1 if your compiler has synchronization intrinsics])
fi])
