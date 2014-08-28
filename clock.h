/* clocks and timers */

#ifndef CLOCK_H
#define CLOCK_H

#include "status.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef long microsec;

status clock_sleep(microsec usec);
status clock_time(microsec* pusec);

#ifdef __cplusplus
}
#endif

#endif
