/* reliable multicast data receiver */

#ifndef RECEIVER_H
#define RECEIVER_H

#include "storage.h"

#ifdef __cplusplus
extern "C" {
#endif

struct receiver;
typedef struct receiver* receiver_handle;

status receiver_create(receiver_handle* precv, const char* mmap_file, unsigned q_capacity, const char* tcp_addr, int tcp_port);
void receiver_destroy(receiver_handle* precv);

storage_handle receiver_get_storage(receiver_handle recv);
boolean receiver_is_running(receiver_handle recv);
status receiver_stop(receiver_handle recv);

size_t receiver_get_tcp_gap_count(receiver_handle recv);
size_t receiver_get_tcp_bytes_recv(receiver_handle recv);
size_t receiver_get_mcast_bytes_recv(receiver_handle recv);
size_t receiver_get_mcast_packets_recv(receiver_handle recv);
double receiver_get_mcast_min_latency(receiver_handle recv);
double receiver_get_mcast_max_latency(receiver_handle recv);
double receiver_get_mcast_mean_latency(receiver_handle recv);
double receiver_get_mcast_stddev_latency(receiver_handle recv);

#ifdef __cplusplus
}
#endif

#endif
