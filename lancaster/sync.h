/*
  Copyright (c)2014-2017 Peak6 Investments, LP.
  Copyright (c)2018-2024 Justin Flude.
  Use of this source code is governed by the COPYING file.
*/

/* synchronize and pause accesses to memory */

#ifndef SYNC_H
#define SYNC_H

#include <lancaster/config.h>

#ifdef LANCASTER_HAVE_SYNC_INTRINSICS
#define SYNC_FETCH_AND_OR __sync_fetch_and_or
#define SYNC_LOCK_RELEASE __sync_lock_release
#define SYNC_SYNCHRONIZE __sync_synchronize
#else
#error no definitions for synchronization intrinsics
#endif

#if defined(LANCASTER_X86_64_CPU) || defined(LANCASTER_MIPS_CPU)
#define CPU_RELAX(x) __asm__ __volatile__("pause" ::: "memory")
#elif defined(LANCASTER_ARM_CPU)
#define CPU_RELAX(x) __asm__ __volatile__("nop" ::: "memory")
#elif defined(LANCASTER_PPC_CPU)
#define CPU_RELAX(x) __asm__ __volatile__("or 27, 27, 27" ::: "memory")
#else
#define CPU_RELAX(x) ((void)0)
#endif

#endif
