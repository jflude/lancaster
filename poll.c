#include "poll.h"
#include "error.h"
#include "xalloc.h"
#include <errno.h>
#include <poll.h>

struct poll_t
{
	nfds_t size;
	nfds_t free_slot;
	struct pollfd* fds;
	sock_handle* socks;
};

status poll_create(poll_handle* ppoller, int nsock)
{
	if (!ppoller || nsock <= 0) {
		error_invalid_arg("poll_create");
		return FAIL;
	}

	*ppoller = XMALLOC(struct poll_t);
	if (!*ppoller)
		return NO_MEMORY;

	BZERO(*ppoller);

	(*ppoller)->fds = xcalloc(nsock, sizeof(struct pollfd));
	if (!(*ppoller)->fds) {
		poll_destroy(ppoller);
		return NO_MEMORY;
	}

	(*ppoller)->socks = xcalloc(nsock, sizeof(sock_handle));
	if (!(*ppoller)->socks) {
		poll_destroy(ppoller);
		return NO_MEMORY;
	}

	(*ppoller)->size = nsock;
	(*ppoller)->free_slot = 0;
	return OK;
}

void poll_destroy(poll_handle* ppoller)
{
	if (!ppoller || !*ppoller)
		return;

	xfree((*ppoller)->socks);
	xfree((*ppoller)->fds);
	xfree(*ppoller);
	*ppoller = NULL;
}

status poll_add(poll_handle poller, sock_handle sock, short events)
{
	struct pollfd* fds;
	if (poller->free_slot == poller->size) {
		struct pollfd* new_fds;
		sock_handle* new_socks;
		int n = 2 * poller->size;

		new_fds = xrealloc(poller->fds, n * sizeof(struct pollfd));
		if (!new_fds)
			return NO_MEMORY;

		poller->fds = new_fds;

		new_socks = xrealloc(poller->socks, n * sizeof(sock_handle));
		if (!new_socks)
			return NO_MEMORY;

		poller->socks = new_socks;
		poller->size = n;
	}

	fds = &poller->fds[poller->free_slot];

	fds->fd = sock_get_descriptor(sock);
	fds->events = events;
	fds->revents = 0;

	poller->socks[poller->free_slot] = sock;
	poller->free_slot++;
	return OK;
}

status poll_remove(poll_handle poller, sock_handle sock)
{
	int i;
	for (i = 0; i < poller->free_slot; ++i)
		if (poller->socks[i] == sock) {
			int j = poller->free_slot - 1;
			if (i != j) {
				poller->fds[i] = poller->fds[j];
				poller->socks[i] = poller->socks[j];
			}

			poller->free_slot--;
			return OK;
		}

	return FAIL;
}

status poll_set_event(poll_handle poller, sock_handle sock, short new_events)
{
	int i;
	for (i = 0; i < poller->free_slot; ++i)
		if (poller->socks[i] == sock) {
			poller->fds[i].events = new_events;
			return OK;
		}

	return FAIL;
}

status poll_events(poll_handle poller, int timeout)
{
	status st;
loop:
	st = poll(poller->fds, poller->free_slot, timeout);
	if (st == -1) {
		if (errno == EINTR)
			goto loop;

		error_errno("poll_events");
	}

	return st;
}

status poll_process(poll_handle poller, poll_proc proc, void* param)
{
	int i;
	for (i = poller->free_slot - 1; i >= 0; --i) {
		status st = proc(poller, poller->socks[i], &poller->fds[i].events, param);
		if (FAILED(st))
			return st;
	}

	return OK;
}

status poll_process_events(poll_handle poller, poll_proc proc, void* param)
{
	int i;
	for (i = poller->free_slot - 1; i >= 0; --i) {
		if (poller->fds[i].revents) {
			status st = proc(poller, poller->socks[i], &poller->fds[i].revents, param);
			if (FAILED(st))
				return st;
		}
	}

	return OK;
}
