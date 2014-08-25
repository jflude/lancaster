/* read and write spin (versioning) locks */

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
	} while (0);

#define SPIN_READ_LOCK(lock, ver) \
	do { \
		int n = 0; \
		long no_ver; \
		(void) no_ver; \
		while ((ver = *(lock)) < 0) \
			if ((++n & MAX_SPINS) != 0) \
				CPU_RELAX(); \
			else \
				yield(); \
	} while (0);

#define SPIN_WRITE_LOCK(lock, old_ver) \
	do { \
		int n = 0; \
		long no_ver; \
		(void) no_ver; \
		while ((old_ver = SYNC_FETCH_AND_OR(lock, SPIN_MASK(lock))) < 0) \
			if ((++n & MAX_SPINS) != 0) \
				CPU_RELAX(); \
			else \
				yield(); \
	} while (0);

#define SPIN_UNLOCK(lock, new_ver) \
	do { \
		int no_ver = 0; \
		(void) no_ver; \
		SYNC_SYNCHRONIZE(); \
		*(lock) = new_ver; \
	} while (0);

#endif
