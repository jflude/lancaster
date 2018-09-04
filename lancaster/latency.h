/*
  Copyright (c)2014-2018 Peak6 Investments, LP.  All rights reserved.
  Use of this source code is governed by the COPYING file.
*/

/* latency statistic calculations */

#ifndef LATENCY_H
#define LATENCY_H

#include <lancaster/status.h>

#ifdef __cplusplus
extern "C" {
#endif

struct latency;
typedef struct latency *latency_handle;

status latency_create(latency_handle *plat);
status latency_destroy(latency_handle *plat);

status latency_on_sample(latency_handle lat, double new_val);
status latency_roll(latency_handle lat);

long latency_get_count(latency_handle lat);
double latency_get_min(latency_handle lat);
double latency_get_max(latency_handle lat);
double latency_get_mean(latency_handle lat);
double latency_get_stddev(latency_handle lat);

#ifdef __cplusplus
}
#endif

#endif
