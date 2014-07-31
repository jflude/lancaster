/* convert between network and host byte order */

#ifndef H2N2H_H
#define H2N2H_H

#include <arpa/inet.h>

#if __BYTE_ORDER == __BIG_ENDIAN

#define htonll(x) (x)
#define ntohll(x) (x)

#elif __BYTE_ORDER == __LITTLE_ENDIAN

#define htonll(x) __bswap_64(x)
#define ntohll(x) __bswap_64(x)

#else

#error unrecognised value for __BYTE_ORDER

#endif

#endif
