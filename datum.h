/* data shared between servers and clients */

#ifndef DATUM_H
#define DATUM_H

#ifdef __cplusplus
extern "C" {
#endif

struct datum_t
{
	double bid_price;
	double ask_price;
	int bid_qty;
	int ask_qty;
};

#define MAX_ID 1000000
#define Q_CAPACITY 131072

#define HB_SEND_PERIOD 10
#define HB_RECV_PERIOD 20

#ifdef __cplusplus
}
#endif

#endif
