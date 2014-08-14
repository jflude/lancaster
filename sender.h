/* reliable multicast data sender */

#ifndef SENDER_H
#define SENDER_H

#include "clock.h"
#include "sock.h"
#include "storage.h"

#ifdef __cplusplus
extern "C" {
#endif

struct sender_t;
typedef struct sender_t* sender_handle;

status sender_create(sender_handle* psend, storage_handle store, microsec_t hb_usec, microsec_t max_age_usec,
					 boolean conflate_packet, const char* mcast_addr, int mcast_port, int mcast_ttl,
					 const char* tcp_addr, int tcp_port);
void sender_destroy(sender_handle* psend);

storage_handle sender_get_storage(sender_handle send);
sock_handle sender_get_listen_socket(sender_handle send);

status sender_record_changed(sender_handle send, record_handle rec);
status sender_flush(sender_handle send);
boolean sender_is_running(sender_handle send);
status sender_stop(sender_handle send);

size_t sender_get_tcp_gap_count(sender_handle send);
size_t sender_get_tcp_bytes_sent(sender_handle send);
size_t sender_get_mcast_bytes_sent(sender_handle send);
size_t sender_get_mcast_packets_sent(sender_handle send);
size_t sender_get_subscriber_count(sender_handle send);

#ifdef __cplusplus
}
#endif

#endif
