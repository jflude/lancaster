/*
  Copyright (c)2014-2017 Peak6 Investments, LP.
  Copyright (c)2018-2024 Justin Flude.
  Use of this source code is governed by the COPYING file.
*/

/* 64 bit signed integers */

#ifndef INT64_H
#define INT64_H

#include <lancaster/config.h>

#ifdef LANCASTER_HAVE_INTTYPES_H
#include <inttypes.h>
#endif

#if !defined(INT64_MAX) && !defined(int64_t)
#error no definition for int64_t
#endif

#endif
