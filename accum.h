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

status accum_create(accum_handle* pacc, size_t capacity, long max_age_usec);
void accum_destroy(accum_handle* pacc);

boolean accum_is_empty(accum_handle acc);
status accum_is_stale(accum_handle acc);
size_t accum_get_avail(accum_handle acc);

status accum_store(accum_handle acc, const void* data, size_t size, void** pstored);
status accum_get_batched(accum_handle acc, const void** pdata, size_t* psize);
void accum_clear(accum_handle acc);

#ifdef __cplusplus
}
#endif

#endif
