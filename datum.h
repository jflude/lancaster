/* data shared between servers and clients */

#ifndef DATUM_H
#define DATUM_H

#include "clock.h"

#define MAX_ID 1000000
#define CONFLATE_PKT FALSE
#define MAX_AGE_USEC 10000

#ifdef __cplusplus
extern "C" {
#endif

struct datum_t
{
	long xyz;
	microsec_t ts;
};

typedef struct datum_t datum;

#ifdef __cplusplus
}
#endif

#endif
