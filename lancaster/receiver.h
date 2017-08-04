/*
   Copyright (c)2014-2017 Peak6 Investments, LP.  All rights reserved.
   Use of this source code is governed by the LICENSE file.
*/

/* reliable multicast data receiver */

#ifndef RECEIVER_H
#define RECEIVER_H

#include <lancaster/storage.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SENDER_QUEUE_CAPACITY ((size_t)-1)

struct receiver;
typedef struct receiver *receiver_handle;

status receiver_create(receiver_handle *precv, const char *mmap_file,
		       mode_t mode_flags, size_t property_size,
		       size_t q_capacity, microsec touch_period_usec,
		       unsigned max_missed_hb, const char *tcp_address,
		       unsigned short tcp_port);
status receiver_destroy(receiver_handle *precv);

storage_handle receiver_get_storage(receiver_handle recv);

status receiver_run(receiver_handle recv);
void receiver_stop(receiver_handle recv);

long receiver_get_tcp_gap_count(receiver_handle recv);
long receiver_get_tcp_bytes_recv(receiver_handle recv);
long receiver_get_mcast_bytes_recv(receiver_handle recv);
long receiver_get_mcast_packets_recv(receiver_handle recv);

double receiver_get_mcast_min_latency(receiver_handle recv);
double receiver_get_mcast_max_latency(receiver_handle recv);
double receiver_get_mcast_mean_latency(receiver_handle recv);
double receiver_get_mcast_stddev_latency(receiver_handle recv);

status receiver_roll_stats(receiver_handle recv);

#ifdef __cplusplus
}
#endif

#endif
