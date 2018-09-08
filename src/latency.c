/*
  Copyright (c)2014-2017 Peak6 Investments, LP.  All rights reserved.
  Use of this source code is governed by the COPYING file.
*/

#include <lancaster/error.h>
#include <lancaster/latency.h>
#include <lancaster/spin.h>
#include <lancaster/xalloc.h>
#include <math.h>

struct stats {
    long count;
    double min;
    double max;
    double mean;
    double M2;
    double stddev;
};

struct latency {
    struct stats *curr;
    struct stats *next;
    volatile spin_lock lock;
};

status latency_create(latency_handle *plat)
{
    if (!plat)
	return error_invalid_arg("latency_create");

    *plat = XMALLOC(struct latency);
    if (!*plat)
	return NO_MEMORY;

    (*plat)->curr = XMALLOC(struct stats);
    if (!(*plat)->curr)
	return NO_MEMORY;

    (*plat)->next = XMALLOC(struct stats);
    if (!(*plat)->next)
	return NO_MEMORY;

    BZERO((*plat)->curr);
    BZERO((*plat)->next);
    spin_create(&(*plat)->lock);

    return OK;
}

status latency_destroy(latency_handle *plat)
{
    if (!plat || !*plat)
	return OK;

    xfree((*plat)->curr);
    xfree((*plat)->next);
    XFREE(*plat);
    return OK;
}

status latency_on_sample(latency_handle lat, double new_val)
{
    status st;
    double delta;
    if (FAILED(st = spin_write_lock(&lat->lock, NULL)))
	return st;

    delta = new_val - lat->next->mean;
    lat->next->mean += delta / ++lat->next->count;
    lat->next->M2 += delta * (new_val - lat->next->mean);

    if (lat->next->min == 0 || new_val < lat->next->min)
	lat->next->min = new_val;

    if (lat->next->max == 0 || new_val > lat->next->max)
	lat->next->max = new_val;

    spin_unlock(&lat->lock, 0);
    return OK;
}

status latency_roll(latency_handle lat)
{
    status st;
    struct stats *tmp;
    if (FAILED(st = spin_write_lock(&lat->lock, NULL)))
	return st;

    tmp = lat->next;
    lat->next = lat->curr;
    lat->curr = tmp;

    BZERO(lat->next);
    spin_unlock(&lat->lock, 0);

    lat->curr->stddev =
	lat->curr->count > 1 ? sqrt(lat->curr->M2 / (lat->curr->count - 1)) : 0;

    return OK;
}

long latency_get_count(latency_handle lat)
{
    return lat->curr->count;
}

double latency_get_min(latency_handle lat)
{
    return lat->curr->min;
}

double latency_get_max(latency_handle lat)
{
    return lat->curr->max;
}

double latency_get_mean(latency_handle lat)
{
    return lat->curr->mean;
}

double latency_get_stddev(latency_handle lat)
{
    return lat->curr->stddev;
}
