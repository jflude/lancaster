/*
   Copyright (c)2014-2017 Peak6 Investments, LP.  All rights reserved.
   Use of this source code is governed by the LICENSE file.
*/

/* portable memory allocation and deallocation */

#ifndef XALLOC_H
#define XALLOC_H

#include <stddef.h>
#include <string.h>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

void *xmalloc(size_t sz);
void *xcalloc(size_t n, size_t sz);
void *xrealloc(void *p, size_t sz);
void xfree(void *p);

char *xstrdup(const char *p);

#define BZERO(p) (memset((p), 0, sizeof(*(p))))
#define COUNTOF(a) (sizeof(a) / sizeof((a)[0]))

#define XMALLOC(t) (xmalloc(sizeof(t)))
#define XFREE(p) \
	do { \
		xfree(p); \
		(p) = NULL; \
	} while (0)

#define DEFAULT_ALIGNMENT 8
#define ALIGNED_SIZE(sz, aln) (((sz) - 1 + (aln)) & ~((aln) - 1))

#ifdef __cplusplus
}
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
