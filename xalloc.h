/* portable memory allocation and deallocation */

#ifndef XALLOC_H
#define XALLOC_H

#include <stddef.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

void* xmalloc(size_t sz);
void* xcalloc(size_t n, size_t sz);
void* xrealloc(void* p, size_t sz);
void xfree(void* p);

char* xstrdup(const char* p);

#define XMALLOC(t) (xmalloc(sizeof(t)))
#define BZERO(p) (memset((p), 0, sizeof(*(p))))
#define COUNTOF(a) (sizeof(a) / sizeof((a)[0]))

#define DEFAULT_ALIGNMENT 8
#define ALIGNED_SIZE(s, a, v, n) \
	(sizeof(s) - a + ((sizeof(((s *) NULL)->v) * (n - 1) + a) & ~(a - 1)))

#ifdef __cplusplus
}
#endif

#endif
