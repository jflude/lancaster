/* multicast service advertiser */

#ifndef ADVERT_H
#define ADVERT_H

#include "sender.h"

#ifdef __cplusplus
extern "C" {
#endif

struct advert_t;
typedef struct advert_t* advert_handle;

status advert_create(advert_handle* padvert, const char* mcast_addr, int mcast_port, int mcast_ttl);
void advert_destroy(advert_handle* padvert);

boolean advert_is_running(advert_handle adv);
status advert_stop(advert_handle adv);

status advert_publish(advert_handle adv, sender_handle send);
status advert_retract(advert_handle adv, sender_handle send);

#ifdef __cplusplus
}
#endif

#endif