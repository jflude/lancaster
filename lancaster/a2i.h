/*
  Copyright (c)2014-2018 Peak6 Investments, LP.  All rights reserved.
  Use of this source code is governed by the COPYING file.
*/

/* convert text to an integer */

#ifndef A2I_H
#define A2I_H

#include <lancaster/status.h>

#ifdef __cplusplus
extern "C" {
#endif

status a2i(const char *text, const char *format, void *pnum);

#ifdef __cplusplus
}
#endif

#endif
