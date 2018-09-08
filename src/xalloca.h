/*
  Copyright (c)2014-2017 Peak6 Investments, LP.  All rights reserved.
  Use of this source code is governed by the COPYING file.
*/

#ifndef XALLOCA_H
#define XALLOCA_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef HAVE_ALLOCA_H
#include <alloca.h>
#elif defined(__GNUC__)
#define alloca __builtin_alloca
#elif defined(_AIX)
#define alloca __alloca
#elif defined(_MSC_VER)
#include <malloc.h>
#define alloca _alloca
#else
#ifdef  __cplusplus
extern "C"
#endif
void *alloca(size_t);
#endif

#endif
