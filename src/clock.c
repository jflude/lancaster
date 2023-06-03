/*
  Copyright (c)2014-2017 Peak6 Investments, LP.  All rights reserved.
  Use of this source code is governed by the COPYING file.
*/

#include <lancaster/clock.h>
#include <lancaster/error.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/time.h>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef HAVE_NANOSLEEP

status clock_sleep(microsec usec)
{
    struct timespec req, rem;
    if (usec < 0)
	return error_invalid_arg("clock_sleep");

    if (usec < 1000000) {
	req.tv_sec = 0;
	req.tv_nsec = usec;
    } else {
	lldiv_t qr = lldiv(usec, 1000000);
	req.tv_sec = qr.quot;
	req.tv_nsec = qr.rem;
    }

    req.tv_nsec *= 1000;
    return nanosleep(&req, &rem) == -1 ? error_eintr("nanosleep") : OK;
}

#else

static status usleep2(microsec usec)
{
    return usleep(usec) == -1 ? error_eintr("usleep") : OK;
}

status clock_sleep(microsec usec)
{
    lldiv_t qr;
    long s;
    if (usec < 0)
	return error_invalid_arg("clock_sleep");

    qr = lldiv(usec, 1000000);

    for (s = qr.quot; s > 0; --s) {
	status st = usleep2(999999);
	if (FAILED(st))
	    return st;
    }

    return usleep2(qr.quot + qr.rem);
}

#endif

#ifdef HAVE_CLOCK_GETTIME

status clock_time(microsec *pusec)
{
    struct timespec ts;
    if (!pusec)
	return error_invalid_arg("clock_time");

    if (clock_gettime(CLOCK_REALTIME, &ts) == -1)
	return error_errno("clock_gettime");

    *pusec = (microsec)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
    return OK;
}

#else

status clock_time(microsec *pusec)
{
    struct timeval tv;
    if (!pusec)
	return error_invalid_arg("clock_time");

    if (gettimeofday(&tv, NULL) == -1)
	return error_errno("gettimeofday");

    *pusec = (microsec)tv.tv_sec * 1000000 + tv.tv_usec;
    return OK;
}

#endif

status clock_get_text(microsec usec, int precision, char *text, size_t text_sz)
{
    time_t t;
    lldiv_t qr;
    struct tm *ptm;
    char fract[32], format[64];
    int last;

    if (!text || text_sz == 0 || precision < 0)
	return error_invalid_arg("clock_get_text");

    qr = lldiv(usec, 1000000);
    t = qr.quot;
    if (!(ptm = localtime(&t)))
	return error_errno("localtime");

    if (sprintf(fract, "%.*f", precision, qr.rem / 1000000.0) < 0 ||
	sprintf(format, "%%Y-%%m-%%dT%%H:%%M:%%S%s%%z ", fract + 1) < 0)
	return error_errno("sprintf");

    if (!strftime(text, text_sz, format, ptm))
	return error_msg(BUFFER_TOO_SMALL, "clock_get_text: buffer too small");

    last = strlen(text) - 1;
    text[last] = text[last - 1];
    text[last - 1] = text[last - 2];
    text[last - 2] = ':';
    return OK;
}

status clock_get_short_text(microsec usec, int precision,
			    char *text, size_t text_sz)
{
    time_t t;
    lldiv_t qr;
    struct tm *ptm;
    char fract[32], format[64];

    if (!text || text_sz == 0 || precision < 0)
	return error_invalid_arg("clock_get_short_text");

    qr = lldiv(usec, 1000000);
    t = qr.quot;
    if (!(ptm = localtime(&t)))
	return error_errno("localtime");

    if (sprintf(fract, "%.*f", precision, qr.rem / 1000000.0) < 0 ||
	sprintf(format, "%%H:%%M:%%S%s", fract + 1) < 0)
	return error_errno("sprintf");

    if (!strftime(text, text_sz, format, ptm))
	return error_msg(BUFFER_TOO_SMALL,
			 "clock_get_short_text: buffer too small");

    return OK;
}
