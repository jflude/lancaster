/* hexadecimal dump to stream */

#ifndef DUMP_H
#define DUMP_H

#include "status.h"
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

status dump(const void* p, size_t sz, boolean relative);
status fdump(const void* p, size_t sz, boolean relative, FILE* f);

#ifdef __cplusplus
}
#endif

#endif
