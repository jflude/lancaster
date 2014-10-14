/* read and write spin/versioning locks */

#ifndef SPIN_H
#define SPIN_H

#include "status.h"
#include <limits.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef long spin_lock;

#define SPIN_MIN LONG_MIN
#define SPIN_MAX LONG_MAX

#define SPIN_MASK (0x80ul << (CHAR_BIT * (sizeof(spin_lock) - 1)))

void spin_create(volatile spin_lock *lock);
status spin_read_lock(volatile spin_lock *lock, spin_lock *old_rev);
status spin_write_lock(volatile spin_lock *lock, spin_lock *old_rev);
void spin_unlock(volatile spin_lock *lock, spin_lock new_rev);

#ifdef __cplusplus
}
#endif

#endif
