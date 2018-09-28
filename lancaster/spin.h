/*
  Copyright (c)2014-2017 Peak6 Investments, LP.  All rights reserved.
  Use of this source code is governed by the COPYING file.
*/

/* read and write spin/versioning locks */

#ifndef SPIN_H
#define SPIN_H

#include <lancaster/int64.h>
#include <lancaster/status.h>
#include <limits.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int64_t spin_lock;

#define SPIN_MIN INT64_MIN
#define SPIN_MAX INT64_MAX

#define SPIN_MASK ((spin_lock)0x80 << (CHAR_BIT * (sizeof(spin_lock) - 1)))

void spin_create(volatile spin_lock *lock);
status spin_read_lock(volatile spin_lock *lock, spin_lock *old_rev);
status spin_write_lock(volatile spin_lock *lock, spin_lock *old_rev);
void spin_unlock(volatile spin_lock *lock, spin_lock new_rev);

#ifdef __cplusplus
}
#endif

#endif
