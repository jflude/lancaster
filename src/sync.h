/*
   Copyright (c)2014-2017 Peak6 Investments, LP.  All rights reserved.
   Use of this source code is governed by the LICENSE file.
*/

/* synchronize and pause accesses to memory */

#ifndef SYNC_H
#define SYNC_H

#define SYNC_FETCH_AND_OR __sync_fetch_and_or
#define SYNC_LOCK_RELEASE __sync_lock_release
#define SYNC_SYNCHRONIZE __sync_synchronize

#define CPU_RELAX(x) __asm__ __volatile__("pause" ::: "memory")

#endif
