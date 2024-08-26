/*
  Copyright (c)2014-2017 Peak6 Investments, LP.
  Use of this source code is governed by the COPYING file.
*/

/* portable socket multiplexing */

#ifndef POLLER_H
#define POLLER_H

#include <lancaster/socket.h>
#include <lancaster/status.h>

#ifdef __cplusplus
extern "C" {
#endif

struct poller;
typedef struct poller *poller_handle;

typedef status (*poller_func)(poller_handle, sock_handle, short *, void *);

status poller_create(poller_handle *ppoller, int nsock);
status poller_destroy(poller_handle *ppoller);

int poller_get_count(poller_handle poller);

status poller_add(poller_handle poller, sock_handle sock, short events);
status poller_remove(poller_handle poller, sock_handle sock);
status poller_set_event(poller_handle poller, sock_handle sock,
			short new_events);
status poller_events(poller_handle poller, int timeout);
status poller_process(poller_handle poller, poller_func fn, void *param);
status poller_process_events(poller_handle poller, poller_func event_fn,
			     void *param);

#ifdef __cplusplus
}
#endif

#endif
