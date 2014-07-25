/* spin lock */

#ifndef SPIN_H
#define SPIN_H

#include "barrier.h"
#include "yield.h"

#define MAX_RELAXES 8191

typedef volatile int spin_lock_t;

#define SPIN_CREATE(p) \
	do { \
		*p = 0; \
	} while (0)

#define SPIN_TRY_LOCK(p) \
	__sync_bool_compare_and_swap(p, 0, 1)

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
		MEMORY_BARRIER(); \
		*p = 0; \
	} while (0)

#endif
