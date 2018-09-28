/*
  Copyright (c)2014-2017 Peak6 Investments, LP.  All rights reserved.
  Use of this source code is governed by the COPYING file.
*/

/* clocks and timers */

#ifndef CLOCK_H
#define CLOCK_H

#include <lancaster/int64.h>
#include <lancaster/status.h>
#include <limits.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int64_t microsec;

#define MICROSEC_MIN INT64_MIN
#define MICROSEC_MAX INT64_MAX

status clock_sleep(microsec usec);
status clock_time(microsec *pusec);

status clock_get_text(microsec usec, int precision,
		      char *text, size_t text_sz);

status clock_get_short_text(microsec usec, int precision,
			    char *text, size_t text_sz);
#ifdef __cplusplus
}
#endif

#endif
