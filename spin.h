/*
   Copyright (C)2014-2016 Peak6 Investments, LP.  All rights reserved.
   Use of this source code is governed by the LICENSE file.
*/

/* read and write spin/versioning locks */

#ifndef SPIN_H
#define SPIN_H

#include <limits.h>
#include "status.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef long spin_lock;

#define SPIN_MIN LONG_MIN
#define SPIN_MAX LONG_MAX

#define SPIN_MASK (0x80uL << (CHAR_BIT * (sizeof(spin_lock) - 1)))

void spin_create(volatile spin_lock *lock);
status spin_read_lock(volatile spin_lock *lock, spin_lock *old_rev);
status spin_write_lock(volatile spin_lock *lock, spin_lock *old_rev);
void spin_unlock(volatile spin_lock *lock, spin_lock new_rev);

#ifdef __cplusplus
}
#endif

#endif
