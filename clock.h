/* clocks and timers */

#ifndef CLOCK_H
#define CLOCK_H

#include "status.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef long microsec;

status clock_sleep(microsec usec);
status clock_time(microsec* pusec);

status clock_get_text(microsec usec, int precision, char* text, size_t text_sz);

#ifdef __cplusplus
}
#endif

#endif
