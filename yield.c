#include "yield.h"
#include "error.h"
#include <errno.h>
#include <sched.h>
#include <time.h>
#include <unistd.h>

status yield(void)
{
#ifdef _POSIX_PRIORITY_SCHEDULING
	if (sched_yield() == -1) {
		error_errno("sched_yield");
		return FAIL;
	}

	return OK;
#else
	return snooze(0, 1000);
#endif
}

status snooze(long sec, long nanosec)
{
	struct timespec req, rem;
	req.tv_sec = sec;
	req.tv_nsec = nanosec;
loop:
	if (nanosleep(&req, &rem) == -1) {
		if (errno == EINTR) {
			req = rem;
			goto loop;
		}

		error_errno("nanosleep");
		return FAIL;
	}

	return OK;
}
