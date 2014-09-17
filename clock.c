#include "clock.h"
#include "error.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/time.h>

#if _POSIX_TIMERS && (_POSIX_TIMERS - 200112L) >= 0

status clock_sleep(microsec usec)
{
	struct timespec req, rem;
	if (usec < 0)
		return error_invalid_arg("clock_sleep");

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

		return error_errno("nanosleep");
	}

	return OK;
}

status clock_time(microsec* pusec)
{
	struct timespec ts;
	if (!pusec)
		return error_invalid_arg("clock_time");

	if (clock_gettime(CLOCK_REALTIME, &ts) == -1)
		return error_errno("clock_gettime");

	*pusec = ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
	return OK;
}

#else

static status usleep2(microsec usec)
{
loop:
	if (usleep(usec) == -1) {
		if (errno == EINTR)
			goto loop;

		return error_errno("usleep");
	}

	return OK;
}

status clock_sleep(microsec usec)
{
	ldiv_t qr;
	long s;
	if (usec < 0)
		return error_invalid_arg("clock_sleep");

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
	if (!pusec)
		return error_invalid_arg("clock_time");

	if (gettimeofday(&tv, NULL) == -1)
		return error_errno("gettimeofday");

	*pusec = tv.tv_sec * 1000000 + tv.tv_usec;
	return OK;
}

#endif

status clock_get_text(microsec usec, char* text, size_t text_sz)
{
	time_t t;
	struct tm* ptm;
	char fract[32];

	if (!text || text_sz == 0)
		return error_invalid_arg("clock_get_text");

	t = usec / 1000000;
	if (!(ptm = localtime(&t)))
		return error_errno("localtime");

	if (!strftime(text, text_sz - 13, "%Y-%m-%dT%H:%M:%S", ptm))
		return error_msg("clock_get_text: buffer too small", BUFFER_TOO_SMALL);

	sprintf(fract, ".%03ld%+03ld:00", (usec % 1000000) / 1000, ptm->tm_gmtoff / (60 * 60));
	strcat(text, fract);
	return OK;
}
