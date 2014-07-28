/* spin lock */

#ifndef SPIN_H
#define SPIN_H

#include "sync.h"
#include "yield.h"

#define MAX_RELAXES 8191

typedef volatile int spin_lock_t;

#define SPIN_CREATE(p) \
	SPIN_UNLOCK(p)

#define SPIN_TRY_LOCK(p) \
	SYNC_BOOL_COMPARE_AND_SWAP(p, 0, 1)

#define SPIN_LOCK(p) \
	do { \
		int n = 0; \
		while (!SPIN_TRY_LOCK(p)) \
			if ((++n & MAX_RELAXES) != 0) \
				CPU_RELAX(); \
			else \
				yield(); \
	} while (0)

#define SPIN_UNLOCK(p) \
	do { \
		SYNC_LOCK_RELEASE(p); \
	} while (0)

#endif
