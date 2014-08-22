#include "clock.h"
#include "error.h"
#include <errno.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <sys/time.h>

#ifdef _POSIX_TIMERS

status clock_sleep(microsec usec)
{
	struct timespec req, rem;
	if (usec < 0) {
		error_invalid_arg("clock_sleep");
		return FAIL;
	}

	if (usec < 1000000) {
		req.tv_sec = 0;
		req.tv_nsec = usec;
	} else {
		ldiv_t qr = ldiv(usec, 1000000);
		req.tv_sec = qr.quot;
		req.tv_nsec = qr.rem;
	}

	req.tv_nsec *= 1000;

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

status clock_time(microsec* pusec)
{
	struct timespec ts;
	if (!pusec) {
		error_invalid_arg("clock_time");
		return FAIL;
	}

	if (clock_gettime(CLOCK_REALTIME, &ts) == -1) {
		error_errno("clock_gettime");
		return FAIL;
	}

	*pusec = ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
	return OK;
}

#else

static status usleep2(useconds usec)
{
loop:
	if (usleep(usec) == -1) {
		if (errno == EINTR)
			goto loop;

		error_errno("usleep");
		return FAIL;
	}

	return OK;
}

status clock_sleep(microsec usec)
{
	ldiv qr;
	long s;
	if (usec < 0) {
		error_invalid_arg("clock_sleep");
		return FAIL;
	}

	qr = ldiv(usec, 1000000);

	for (s = qr.quot; s > 0; --s) {
		status st = usleep2(999999);
		if (FAILED(st))
			return st;
	}

	return usleep2(qr.quot + qr.rem);
}

status clock_time(microsec* pusec)
{
	struct timeval tv;
	if (!pusec) {
		error_invalid_arg("clock_time");
		return FAIL;
	}

	if (gettimeofday(&tv, NULL) == -1) {
		error_errno("gettimeofday");
		return FAIL;
	}

	*pusec = tv.tv_sec * 1000000 + tv.tv_usec;
	return OK;
}

#endif
