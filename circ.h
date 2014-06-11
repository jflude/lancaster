/* lock-less, single producer/consumer circular buffer */

#ifndef CIRC_H
#define CIRC_H

#include "status.h"

#ifdef __cplusplus
extern "C" {
#endif

struct circ_t;
typedef struct circ_t* circ_handle;

status circ_create(circ_handle* pcirc, unsigned capacity);
void circ_destroy(circ_handle* pcirc);

unsigned circ_get_count(circ_handle circ);

status circ_insert(circ_handle circ, void* val);
status circ_remove(circ_handle circ, void** pval);

#ifdef __cplusplus
}
#endif

#endif
