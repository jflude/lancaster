/*
  Copyright (c)2014-2017 Peak6 Investments, LP.
  Copyright (c)2018-2024 Justin Flude.
  Use of this source code is governed by the COPYING file.
*/

/* multicast service advertiser */

#ifndef ADVERT_H
#define ADVERT_H

#include <lancaster/sender.h>

#ifdef __cplusplus
extern "C" {
#endif

struct advert;
typedef struct advert *advert_handle;

status advert_create(advert_handle *padvert, const char *mcast_address,
		     unsigned short mcast_port, const char *mcast_interface,
		     short mcast_ttl, boolean mcast_loopback, const char *env,
		     microsec tx_period_usec);
status advert_destroy(advert_handle *padvert);

boolean advert_is_running(advert_handle adv);
status advert_stop(advert_handle adv);

status advert_publish(advert_handle adv, sender_handle send);
status advert_retract(advert_handle adv, sender_handle send);

#ifdef __cplusplus
}
#endif

#endif
