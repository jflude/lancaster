#include "poller.h"
#include "error.h"
#include "xalloc.h"
#include <errno.h>
#include <poll.h>

struct poller
{
	nfds_t count;
	nfds_t free_slot;
	struct pollfd* fds;
	sock_handle* socks;
};

status poller_create(poller_handle* ppoller, int nsock)
{
	if (!ppoller || nsock <= 0) {
		error_invalid_arg("poller_create");
		return FAIL;
	}

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
	(*ppoller)->free_slot = 0;
	return OK;
}

void poller_destroy(poller_handle* ppoller)
{
	if (!ppoller || !*ppoller)
		return;

	xfree((*ppoller)->socks);
	xfree((*ppoller)->fds);
	xfree(*ppoller);
	*ppoller = NULL;
}

int poller_get_count(poller_handle poller)
{
	return poller->free_slot;
}

status poller_add(poller_handle poller, sock_handle sock, short events)
{
	struct pollfd* fds;
	if (!sock) {
		error_invalid_arg("poller_add");
		return FAIL;
	}

	if (poller->free_slot == poller->count) {
		struct pollfd* new_fds;
		sock_handle* new_socks;
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

	fds = &poller->fds[poller->free_slot];

	fds->fd = sock_get_descriptor(sock);
	fds->events = events;
	fds->revents = 0;

	poller->socks[poller->free_slot] = sock;
	++poller->free_slot;
	return OK;
}

status poller_remove(poller_handle poller, sock_handle sock)
{
	nfds_t i;
	if (!sock) {
		error_invalid_arg("poller_remove");
		return FAIL;
	}

	for (i = 0; i < poller->free_slot; ++i)
		if (poller->socks[i] == sock) {
			nfds_t j = poller->free_slot - 1;
			if (i != j) {
				poller->fds[i] = poller->fds[j];
				poller->socks[i] = poller->socks[j];
			}

			poller->free_slot--;
			return OK;
		}

	return FAIL;
}

status poller_set_event(poller_handle poller, sock_handle sock, short new_events)
{
	nfds_t i;
	if (!sock) {
		error_invalid_arg("poller_set_event");
		return FAIL;
	}

	for (i = 0; i < poller->free_slot; ++i)
		if (poller->socks[i] == sock) {
			poller->fds[i].events = new_events;
			return OK;
		}

	return FAIL;
}

status poller_events(poller_handle poller, int timeout)
{
	status st;
loop:
	st = poll(poller->fds, poller->free_slot, timeout);
	if (st == -1) {
		if (errno == EINTR)
			goto loop;

		error_errno("poller_events");
	}

	return st;
}

status poller_process(poller_handle poller, poller_func fn, void* param)
{
	int i;
	if (!fn) {
		error_invalid_arg("poller_process");
		return FAIL;
	}

	for (i = poller->free_slot - 1; i >= 0; --i) {
		status st = fn(poller, poller->socks[i], &poller->fds[i].events, param);
		if (FAILED(st))
			return st;
	}

	return OK;
}

status poller_process_events(poller_handle poller, poller_func fn, void* param)
{
	int i;
	if (!fn) {
		error_invalid_arg("poller_process");
		return FAIL;
	}

	for (i = poller->free_slot - 1; i >= 0; --i)
		if (poller->fds[i].revents) {
			status st = fn(poller, poller->socks[i], &poller->fds[i].revents, param);
			if (FAILED(st))
				return st;
		}

	return OK;
}
