/*
   Copyright (c)2014-2016 Peak6 Investments, LP.  All rights reserved.
   Use of this source code is governed by the LICENSE file.
*/

/* Mersenne Twister - thread-safe pseudorandom number generator */

#ifndef TWIST_H
#define TWIST_H

#include "status.h"

#ifdef __cplusplus
extern "C" {
#endif

struct twist;
typedef struct twist *twist_handle;

status twist_create(twist_handle * ptwist);
status twist_destroy(twist_handle * ptwist);

void twist_seed(twist_handle twist, unsigned seed);
unsigned twist_rand(twist_handle twist);

#ifdef __cplusplus
}
#endif

#endif
