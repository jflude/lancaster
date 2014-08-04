/* thread-safe circular buffer */

#ifndef CIRC_H
#define CIRC_H

#include "status.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

struct circ_t;
typedef struct circ_t* circ_handle;

status circ_create(circ_handle* pcirc, size_t capacity);
void circ_destroy(circ_handle* pcirc);

size_t circ_get_count(circ_handle circ);

status circ_insert(circ_handle circ, void* val);
status circ_remove(circ_handle circ, void** pval);

#ifdef __cplusplus
}
#endif

#endif
