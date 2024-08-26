/*
  Copyright (c)2014-2017 Peak6 Investments, LP.
  Use of this source code is governed by the COPYING file.
*/

#include <lancaster/error.h>
#include <lancaster/clock.h>
#include <lancaster/spin.h>
#include <lancaster/sync.h>

#define MAX_SPINS 10000
#define MAX_WAIT_USEC (1 * 1000000)
#define SLEEP_USEC 1000

void spin_create(volatile spin_lock *lock)
{
    SYNC_LOCK_RELEASE(lock);
}

status spin_read_lock(volatile spin_lock *lock, spin_lock *old_rev)
{
    spin_lock rev;
    int spins = 0, sleeps = 0;

    while ((rev = *lock) < 0)
	if (++spins <= MAX_SPINS) {
	    CPU_RELAX();
	} else {
	    status st;
	    if (++sleeps > (MAX_WAIT_USEC / SLEEP_USEC))
		return error_msg(DEADLOCK_DETECTED,
				 "spin_read_lock: deadlock detected");

	    if (FAILED(st = clock_sleep(SLEEP_USEC)))
		return st;
	}

    if (old_rev)
	*old_rev = rev;

    return OK;
}

status spin_write_lock(volatile spin_lock *lock, spin_lock *old_rev)
{
    spin_lock rev;
    int spins = 0, sleeps = 0;

    while ((rev = SYNC_FETCH_AND_OR(lock, SPIN_MASK)) < 0)
	if (++spins <= MAX_SPINS) {
	    CPU_RELAX();
	} else {
	    status st;
	    if (++sleeps > (MAX_WAIT_USEC / SLEEP_USEC))
		return error_msg(DEADLOCK_DETECTED,
				 "spin_write_lock: deadlock detected");

	    if (FAILED(st = clock_sleep(SLEEP_USEC)))
		return st;
	}

    if (old_rev)
	*old_rev = rev;

    return OK;
}

void spin_unlock(volatile spin_lock *lock, spin_lock new_rev)
{
    SYNC_SYNCHRONIZE();
    *lock = new_rev;
}
