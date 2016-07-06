#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/time.h>
#include "clock.h"
#include "error.h"

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
	return nanosleep(&req, &rem) == -1 ? error_eintr("nanosleep") : OK;
}

status clock_time(microsec *pusec)
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
	return usleep(usec) == -1 ? error_eintr("usleep") : OK;
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

status clock_time(microsec *pusec)
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

status clock_get_text(microsec usec, int precision,
					  char *text, size_t text_sz)
{
	time_t t;
	struct tm *ptm;
	char fract[32];

	if (!text || text_sz == 0 || precision < 0)
		return error_invalid_arg("clock_get_text");

	t = usec / 1000000;
	if (!(ptm = localtime(&t)))
		return error_errno("localtime");

	if (!strftime(text, text_sz - precision - 7, "%Y-%m-%dT%H:%M:%S", ptm))
		return error_msg(BUFFER_TOO_SMALL, "clock_get_text: buffer too small");

	if (sprintf(fract, "%.*f%+03ld:00", precision,
				(usec % 1000000) / 1000000.0, ptm->tm_gmtoff / (60 * 60)) < 0)
		return error_errno("sprintf");

	strcat(text, fract + 1);
	return OK;
}

status clock_get_short_text(microsec usec, int precision,
							char *text, size_t text_sz)
{
	time_t t;
	struct tm *ptm;
	char fract[32];

	if (!text || text_sz == 0 || precision < 0)
		return error_invalid_arg("clock_get_short_text");

	t = usec / 1000000;
	if (!(ptm = localtime(&t)))
		return error_errno("localtime");

	if (!strftime(text, text_sz - precision - 1, "%H:%M:%S", ptm))
		return error_msg(BUFFER_TOO_SMALL,
						 "clock_get_short_text: buffer too small");

	if (sprintf(fract, "%.*f", precision, (usec % 1000000) / 1000000.0) < 0)
		return error_errno("sprintf");

	strcat(text, fract + 1);
	return OK;
}
