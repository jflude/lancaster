/* reliable multicast data receiver */

#ifndef RECEIVER_H
#define RECEIVER_H

#include "storage.h"

#ifdef __cplusplus
extern "C" {
#endif

struct receiver_t;
typedef struct receiver_t* receiver_handle;

status receiver_create(receiver_handle* precv, unsigned q_capacity, const char* tcp_addr, int tcp_port);
void receiver_destroy(receiver_handle* precv);

storage_handle receiver_get_storage(receiver_handle recv);
status receiver_record_changed(receiver_handle recv, record_handle* prec);
unsigned receiver_get_queue_count(receiver_handle recv);

boolean receiver_is_running(receiver_handle recv);
status receiver_stop(receiver_handle recv);

long receiver_get_tcp_gap_count(receiver_handle recv);
long receiver_get_tcp_bytes_recv(receiver_handle recv);
long receiver_get_mcast_bytes_recv(receiver_handle recv);

#ifdef __cplusplus
}
#endif

#endif
