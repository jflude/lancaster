/*
   Copyright (c)2014-2017 Peak6 Investments, LP.  All rights reserved.
   Use of this source code is governed by the COPYING file.
*/

#include <lancaster/error.h>
#include <lancaster/xalloc.h>
#include <errno.h>
#include <stdlib.h>

void *xmalloc(size_t sz)
{
    void *p;
    if (sz == 0)
	return NULL;

    p = malloc(sz);

    if (!p) {
	errno = ENOMEM;
	error_errno("malloc");
    }

    return p;
}

void *xcalloc(size_t n, size_t sz)
{
    void *p;
    if (sz == 0)
	return NULL;

    p = calloc(n, sz);

    if (!p) {
	errno = ENOMEM;
	error_errno("calloc");
    }

    return p;
}

void *xrealloc(void *p, size_t sz)
{
    if (!p && sz > 0)
	return xmalloc(sz);

    p = realloc(p, sz);

    if (!p && sz > 0) {
	errno = ENOMEM;
	error_errno("realloc");
    }

    return p;
}

void xfree(void *p)
{
    if (p)
	free(p);
}

char *xstrdup(const char *p)
{
    char *q;
    if (!p)
	return NULL;

    q = xmalloc(strlen(p) + 1);
    return q ? strcpy(q, p) : NULL;
}
