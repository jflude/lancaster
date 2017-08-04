/*
   Copyright (c)2014-2017 Peak6 Investments, LP.  All rights reserved.
   Use of this source code is governed by the LICENSE file.
*/

#include <lancaster/error.h>
#include <lancaster/poller.h>
#include <lancaster/xalloc.h>
#include <errno.h>
#include <poll.h>

struct poller {
    nfds_t count;
    nfds_t free_idx;
    struct pollfd *fds;
    sock_handle *socks;
};

status poller_create(poller_handle *ppoller, int nsock)
{
    if (!ppoller || nsock <= 0)
	return error_invalid_arg("poller_create");

    *ppoller = XMALLOC(struct poller);
    if (!*ppoller)
	return NO_MEMORY;

    BZERO(*ppoller);

    (*ppoller)->fds = xcalloc(nsock, sizeof(struct pollfd));
    if (!(*ppoller)->fds) {
	poller_destroy(ppoller);
	return NO_MEMORY;
    }

    (*ppoller)->socks = xcalloc(nsock, sizeof(sock_handle));
    if (!(*ppoller)->socks) {
	poller_destroy(ppoller);
	return NO_MEMORY;
    }

    (*ppoller)->count = nsock;
    (*ppoller)->free_idx = 0;
    return OK;
}

status poller_destroy(poller_handle *ppoller)
{
    if (!ppoller || !*ppoller)
	return OK;

    xfree((*ppoller)->socks);
    xfree((*ppoller)->fds);
    XFREE(*ppoller);
    return OK;
}

int poller_get_count(poller_handle poller)
{
    return poller->free_idx;
}

status poller_add(poller_handle poller, sock_handle sock, short events)
{
    struct pollfd *fds;
    if (!sock)
	return error_invalid_arg("poller_add");

    if (poller->free_idx == poller->count) {
	struct pollfd *new_fds;
	sock_handle *new_socks;
	int n = 2 * poller->count;

	new_fds = xrealloc(poller->fds, n * sizeof(struct pollfd));
	if (!new_fds)
	    return NO_MEMORY;

	poller->fds = new_fds;

	new_socks = xrealloc(poller->socks, n * sizeof(sock_handle));
	if (!new_socks)
	    return NO_MEMORY;

	poller->socks = new_socks;
	poller->count = n;
    }

    fds = &poller->fds[poller->free_idx];

    fds->fd = sock_get_descriptor(sock);
    fds->events = events;
    fds->revents = 0;

    poller->socks[poller->free_idx] = sock;
    ++poller->free_idx;
    return OK;
}

status poller_remove(poller_handle poller, sock_handle sock)
{
    nfds_t i;
    if (!sock)
	return error_invalid_arg("poller_remove");

    for (i = 0; i < poller->free_idx; ++i)
	if (poller->socks[i] == sock) {
	    nfds_t j = poller->free_idx - 1;
	    if (i != j) {
		poller->fds[i] = poller->fds[j];
		poller->socks[i] = poller->socks[j];
	    }

	    poller->free_idx--;
	    return OK;
	}

    return NOT_FOUND;
}

status poller_set_event(poller_handle poller, sock_handle sock,
			short new_events)
{
    nfds_t i;
    if (!sock)
	return error_invalid_arg("poller_set_event");

    for (i = 0; i < poller->free_idx; ++i)
	if (poller->socks[i] == sock) {
	    poller->fds[i].events = new_events;
	    return OK;
	}

    return NOT_FOUND;
}

status poller_events(poller_handle poller, int timeout)
{
    status st = poll(poller->fds, poller->free_idx, timeout);
    return st == -1 ? error_eintr("poll") : st;
}

status poller_process(poller_handle poller, poller_func fn, void *param)
{
    int i;
    status st = OK;
    if (!fn)
	return error_invalid_arg("poller_process");

    for (i = poller->free_idx - 1; i >= 0; --i)
	if (FAILED(st = fn(poller, poller->socks[i],
			   &poller->fds[i].events, param)))
	    break;

    return st;
}

status poller_process_events(poller_handle poller, poller_func event_fn,
			     void *param)
{
    int i;
    status st = OK;
    if (!event_fn)
	return error_invalid_arg("poller_process");

    for (i = poller->free_idx - 1; i >= 0; --i)
	if (poller->fds[i].revents &&
	    FAILED(st = event_fn(poller, poller->socks[i],
				 &poller->fds[i].revents, param)))
	    break;

    return st;
}
