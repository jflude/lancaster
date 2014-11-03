/* convert between network and host byte order */

#ifndef H2N2H_H
#define H2N2H_H

#include <arpa/inet.h>
#include <sys/types.h>

#if __BYTE_ORDER == __BIG_ENDIAN

#undef htonll
#undef ntohll

#define htonll(x) (x)
#define ntohll(x) (x)

#elif __BYTE_ORDER == __LITTLE_ENDIAN

#undef htonll
#undef ntohll

#define htonll(x) __builtin_bswap64(x)
#define ntohll(x) __builtin_bswap64(x)

#else

#ifndef MAKE_DEPEND
#error unrecognised value for __BYTE_ORDER
#endif

#endif

#endif
