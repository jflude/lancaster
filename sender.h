/* reliable multicast data sender */

#ifndef SENDER_H
#define SENDER_H

#include "clock.h"
#include "sock.h"
#include "storage.h"

#ifdef __cplusplus
extern "C" {
#endif

struct sender;
typedef struct sender* sender_handle;

status sender_create(sender_handle* psender, const char* mmap_file, microsec heartbeat_usec, microsec max_pkt_age_usec,
					 const char* mcast_addr, int mcast_port, int mcast_ttl, const char* tcp_addr, int tcp_port);
void sender_destroy(sender_handle* psender);

storage_handle sender_get_storage(sender_handle sender);
int sender_get_listen_port(sender_handle sender);

status sender_run(sender_handle sender);
void sender_stop(sender_handle sender);

size_t sender_get_tcp_gap_count(sender_handle sender);
size_t sender_get_tcp_bytes_sent(sender_handle sender);
size_t sender_get_mcast_bytes_sent(sender_handle sender);
size_t sender_get_mcast_packets_sent(sender_handle sender);
size_t sender_get_receiver_count(sender_handle sender);

#ifdef __cplusplus
}
#endif

#endif
