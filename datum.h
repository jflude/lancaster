/* data shared between servers and clients */

#ifndef DATUM_H
#define DATUM_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct datum_t
{
	long bidPrice;
	long askPrice;
	int bidSize;
	int askSize;
	unsigned long opraSeq;
} datum;

#define MAX_ID 1000000
#define HB_PERIOD 10

#ifdef __cplusplus
}
#endif

#endif
