/* synchronize accesses to memory */

#ifndef SYNC_H
#define SYNC_H

#define SYNC_FETCH_AND_OR __sync_fetch_and_or
#define SYNC_BOOL_COMPARE_AND_SWAP __sync_bool_compare_and_swap
#define SYNC_LOCK_RELEASE __sync_lock_release
#define SYNC_SYNCHRONIZE __sync_synchronize

#endif
