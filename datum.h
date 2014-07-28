/* data shared between servers and clients */

#ifndef DATUM_H
#define DATUM_H

#define MAX_ID 1000000
#define HEARTBEAT_SEC 10
#define CONFLATE_PKT TRUE
#define MAX_AGE_USEC 1000

#ifdef __cplusplus
extern "C" {
#endif

struct datum_t
{
	long bidPrice;
	long askPrice;
	int bidSize;
	int askSize;
	unsigned long opraSeq;
};

typedef struct datum_t datum;

#ifdef __cplusplus
}
#endif

#endif
