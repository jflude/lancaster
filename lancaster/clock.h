/*
  Copyright (c)2014-2017 Peak6 Investments, LP.
  Copyright (c)2018-2024 Justin Flude.
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

/* RFC 3339, Section 5.6, Internet Date/Time Format */
status clock_get_text(microsec usec, int precision,
		      char *text, size_t text_sz);

/* HH:MM:SS */
status clock_get_short_text(microsec usec, int precision,
			    char *text, size_t text_sz);
#ifdef __cplusplus
}
#endif

#endif
