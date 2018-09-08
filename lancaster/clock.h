/*
  Copyright (c)2014-2017 Peak6 Investments, LP.  All rights reserved.
  Use of this source code is governed by the COPYING file.
*/

/* clocks and timers */

#ifndef CLOCK_H
#define CLOCK_H

#include <lancaster/status.h>
#include <limits.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef long microsec;

#define MICROSEC_MIN LONG_MIN
#define MICROSEC_MAX LONG_MAX

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
