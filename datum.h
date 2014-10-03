/* data shared between servers and clients */

#ifndef DATUM_H
#define DATUM_H

#include "clock.h"

#define MAX_ID 1000

#ifdef __cplusplus
extern "C" {
#endif

struct datum
{
	long xyz;
};

typedef struct datum datum_t;

#ifdef __cplusplus
}
#endif

#endif
