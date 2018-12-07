/*
  Copyright (c)2014-2017 Peak6 Investments, LP.  All rights reserved.
  Use of this source code is governed by the COPYING file.
*/

/* convert between network and host byte order */

#ifndef H2N2H_H
#define H2N2H_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <arpa/inet.h>
#include <sys/types.h>

#undef htonll
#undef ntohll

#ifdef WORDS_BIGENDIAN

#define htonll(x) (x)
#define ntohll(x) (x)

#else

#define htonll(x) __builtin_bswap64(x)
#define ntohll(x) __builtin_bswap64(x)

#endif

#endif
