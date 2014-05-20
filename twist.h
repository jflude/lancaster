/* Mersenne Twister - thread-safe pseudorandom number generator */

#ifndef TWIST_H
#define TWIST_H

#include "status.h"

#ifdef __cplusplus
extern "C" {
#endif

struct twist_t;
typedef struct twist_t* twist_handle;

status twist_create(twist_handle* ptwist);
void twist_destroy(twist_handle* ptwist);

void twist_seed(twist_handle twist, unsigned seed);
unsigned twist_rand(twist_handle twist);

#ifdef __cplusplus
}
#endif

#endif
