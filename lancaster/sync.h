/*
  Copyright (c)2014-2017 Peak6 Investments, LP.  All rights reserved.
  Use of this source code is governed by the COPYING file.
*/

/* synchronize and pause accesses to memory */

#ifndef SYNC_H
#define SYNC_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef HAVE_SYNC_INTRINSICS
#define SYNC_FETCH_AND_OR __sync_fetch_and_or
#define SYNC_LOCK_RELEASE __sync_lock_release
#define SYNC_SYNCHRONIZE __sync_synchronize
#else
#error no definitions for synchronization intrinsics
#endif

#if defined(X86_64_CPU)
#define CPU_RELAX(x) __asm__ __volatile__("pause" ::: "memory")
#elif defined(ARM_CPU)
#define CPU_RELAX(x) __asm__ __volatile__("nop" ::: "memory")
#elif defined(MIPS_CPU)
#define CPU_RELAX(x) __asm__ __volatile__("pause" ::: "memory")
#elif defined(PPC_CPU)
#define CPU_RELAX(x) __asm__ __volatile__("or 27, 27, 27" ::: "memory")
#else
#define CPU_RELAX(x) ((void)0)
#endif

#endif
