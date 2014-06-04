/* time-aware data accumulator */

#ifndef ACCUM_H
#define ACCUM_H

#include "status.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

struct accum_t;
typedef struct accum_t* accum_handle;

status accum_create(accum_handle* pacc, size_t capacity, long max_age_usec, size_t conflater_capacity);
void accum_destroy(accum_handle* pacc);

boolean accum_is_empty(accum_handle acc);
status accum_is_stale(accum_handle acc);
boolean accum_is_conflated(accum_handle acc, int id);
size_t accum_get_avail(accum_handle acc);

status accum_store(accum_handle acc, const void* data, size_t size);
status accum_conflate(accum_handle acc, const void* data, size_t size, int id);
status accum_get_batched(accum_handle acc, const void** pdata, size_t* psize);
void accum_clear(accum_handle acc);

#ifdef __cplusplus
}
#endif

#endif
