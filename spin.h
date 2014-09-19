/* read and write spin locks */

#ifndef SPIN_H
#define SPIN_H

#include "sync.h"
#include "yield.h"
#include <limits.h>

#define MAX_SPINS 8191

#define SPIN_MASK(lock) \
	(0x80uL << (CHAR_BIT * sizeof(lock) - 1))

#define SPIN_CREATE(lock) \
	do { \
		SYNC_LOCK_RELEASE(lock); \
	} while (0)

#define SPIN_READ_LOCK(lock, rev) \
	do { \
		int n = 0; \
		long no_rev; \
		(void) no_rev; \
		while ((rev = *(lock)) < 0) \
			if ((++n & MAX_SPINS) != 0) \
				CPU_RELAX(); \
			else \
				yield(); \
	} while (0)

#define SPIN_WRITE_LOCK(lock, old_rev) \
	do { \
		int n = 0; \
		long no_rev; \
		(void) no_rev; \
		while ((old_rev = SYNC_FETCH_AND_OR(lock, SPIN_MASK(lock))) < 0) \
			if ((++n & MAX_SPINS) != 0) \
				CPU_RELAX(); \
			else \
				yield(); \
	} while (0)

#define SPIN_UNLOCK(lock, new_rev) \
	do { \
		int no_rev = 0; \
		(void) no_rev; \
		SYNC_SYNCHRONIZE(); \
		*(lock) = new_rev; \
	} while (0)

#endif
