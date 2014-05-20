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

#define TCP_ADDR "10.2.2.152"
#define TCP_PORT 23266

#define MCAST_ADDR "227.1.1.34"
#define MCAST_PORT 56134

#define Q_CAPACITY 131072

#define HB_SEND_PERIOD 10
#define HB_RECV_PERIOD 20

#define MAX_ID 1000000

#ifdef __cplusplus
}
#endif

#endif
