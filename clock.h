/* clocks and timers */

#ifndef CLOCK_H
#define CLOCK_H

#include "status.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef long microsec_t;

status clock_sleep(microsec_t usec);
status clock_time(microsec_t* pusec);

#ifdef __cplusplus
}
#endif

#endif
