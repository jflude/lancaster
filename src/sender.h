/*
   Copyright (c)2014-2017 Peak6 Investments, LP.  All rights reserved.
   Use of this source code is governed by the LICENSE file.
*/

/* reliable multicast data sender */

#ifndef SENDER_H
#define SENDER_H

#include "clock.h"
#include "status.h"
#include "storage.h"

#ifdef __cplusplus
extern "C" {
#endif

struct sender;
typedef struct sender *sender_handle;

status sender_create(sender_handle * psndr, const char *mmap_file,
		     const char *tcp_address, unsigned short tcp_port,
		     const char *mcast_address, unsigned short mcast_port,
		     const char *mcast_interface, short mcast_ttl,
		     boolean mcast_loopback, boolean ignore_recreate,
		     boolean ignore_overrun, microsec heartbeat_usec,
		     microsec orphan_timeout_usec, microsec max_pkt_age_usec);
status sender_destroy(sender_handle * psndr);

storage_handle sender_get_storage(sender_handle sndr);
unsigned short sender_get_listen_port(sender_handle sndr);

status sender_run(sender_handle sndr);
void sender_stop(sender_handle sndr);

long sender_get_tcp_gap_count(sender_handle sndr);
long sender_get_tcp_bytes_sent(sender_handle sndr);
long sender_get_mcast_bytes_sent(sender_handle sndr);
long sender_get_mcast_packets_sent(sender_handle sndr);
long sender_get_receiver_count(sender_handle sndr);

long sender_get_storage_record_count(sender_handle sndr);
double sender_get_storage_min_latency(sender_handle sndr);
double sender_get_storage_max_latency(sender_handle sndr);
double sender_get_storage_mean_latency(sender_handle sndr);
double sender_get_storage_stddev_latency(sender_handle sndr);

status sender_roll_stats(sender_handle sndr);

#ifdef __cplusplus
}
#endif

#endif
