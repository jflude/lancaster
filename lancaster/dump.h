/*
  Copyright (c)2014-2017 Peak6 Investments, LP.  All rights reserved.
  Use of this source code is governed by the COPYING file.
*/

/* hexadecimal dump to stream */

#ifndef DUMP_H
#define DUMP_H

#include <lancaster/status.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

status dump(const void *p, const void *base, size_t sz);
status fdump(const void *p, const void *base, size_t sz, FILE *f);

#ifdef __cplusplus
}
#endif

#endif
