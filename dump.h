/* hexadecimal dump to stream */

#ifndef DUMP_H
#define DUMP_H

#include <stdio.h>
#include "status.h"

#ifdef __cplusplus
extern "C" {
#endif

status dump(const void *p, const void *base, size_t sz);
status fdump(const void *p, const void *base, size_t sz, FILE *f);

#ifdef __cplusplus
}
#endif

#endif
