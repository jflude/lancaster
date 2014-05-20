/* portable socket multiplexing */

#ifndef POLL_H
#define POLL_H

#include "status.h"
#include "sock.h"

#ifdef __cplusplus
extern "C" {
#endif

struct poll_t;
typedef struct poll_t* poll_handle;

typedef status (*poll_proc)(poll_handle, sock_handle, short*, void*);

status poll_create(poll_handle* ppoller, int nsock);
void poll_destroy(poll_handle* ppoller);

status poll_add(poll_handle poller, sock_handle sock, short events);
status poll_remove(poll_handle poller, sock_handle sock);
status poll_set_event(poll_handle poller, sock_handle sock, short new_events);
status poll_events(poll_handle poller, int timeout);
status poll_process(poll_handle poller, poll_proc proc, void* param);
status poll_process_events(poll_handle poller, poll_proc proc, void* param);

#ifdef __cplusplus
}
#endif

#endif
