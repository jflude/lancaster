/* clocks and timers */

#ifndef CLOCK_H
#define CLOCK_H

#include <limits.h>
#include <stddef.h>
#include "status.h"

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
