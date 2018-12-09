AC_DEFUN([AC_SYS_OS_CPU_TYPES], [
AC_MSG_NOTICE([determining types of host operating system and CPU])
AC_CANONICAL_HOST
AS_CASE([$host],
	[*-*-cygwin*], AC_DEFINE([CYGWIN_OS], [1], [Host OS is Cygwin]),
	[*-*-darwin*], AC_DEFINE([DARWIN_OS], [1], [Host OS is Darwin]))
AS_CASE([$host],
	[x86_64*], AC_DEFINE([X86_64_CPU], [1], [Host CPU is x86_64]),
	[arm*], AC_DEFINE([ARM_CPU], [1], [Host CPU is ARM]),
	[mips*], AC_DEFINE([MIPS_CPU], [1], [Host CPU is MIPS]),
	[ppc*], AC_DEFINE([PPC_CPU], [1], [Host CPU is PPC]))])
