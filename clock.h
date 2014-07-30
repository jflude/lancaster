/* clocks and timers */

#ifndef CLOCK_H
#define CLOCK_H

#include "status.h"

typedef long microsec_t;

status clock_sleep(microsec_t usec);
status clock_time(microsec_t* pusec);

#endif
