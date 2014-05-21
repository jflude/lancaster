#include "sender.h"
#include "accum.h"
#include "circ.h"
#include "error.h"
#include "poll.h"
#include "spin.h"
#include "thread.h"
#include "xalloc.h"
#include <alloca.h>
#include <errno.h>
#include <limits.h>
#include <poll.h>
#include <stdio.h>
#include <time.h>
#include <sys/socket.h>

#define MAX_AGE_USEC 10000

struct sender_stats_t
{
	spin_lock_t lock;
	long tcp_gap_count;
	long tcp_bytes_sent;
	long mcast_bytes_sent;
};

struct sender_t
{
	thread_handle mcast_thr;
	thread_handle tcp_thr;
	sock_handle mcast_sock;
	sock_handle listen_sock;
	poll_handle poller;
	circ_handle changed_q;
	accum_handle mcast_accum;
	storage_handle store;
	long next_seq;
	long min_store_seq;
	time_t last_mcast_send;
	int heartbeat;
	size_t hello_len;
	char hello_str[128];
	struct sender_stats_t stats;
};

struct sender_tcp_req_param_t
{
	sender_handle me;
	sock_handle sock;
	size_t val_size;
	size_t pkt_size;
	struct storage_seq_range_t range;
	char* send_buf;
	long* send_seq;
	int* send_id;
	char* next_send;
	size_t remain_send;
	record_handle curr_rec;
	long min_store_seq_seen;
	time_t last_tcp_send;
	boolean sending_hb;
};

static status sender_accum_write(sender_handle me)
{
	const void* data;
	size_t sz;
	status st;

	if (!accum_get_batched(me->mcast_accum, &data, &sz))
		return OK;

	st = sock_sendto(me->mcast_sock, data, sz);
	if (!FAILED(st)) {
		accum_clear(me->mcast_accum);
		me->next_seq++;

		SPIN_LOCK(&me->stats.lock);
		me->stats.mcast_bytes_sent += sz;
		SPIN_UNLOCK(&me->stats.lock);

		me->last_mcast_send = time(NULL);
	}

	return st;
}

static status sender_mcast_on_write(sender_handle me, record_handle rec, size_t val_size)
{
	status st;
	int id;
	if (accum_get_avail(me->mcast_accum) < (val_size + sizeof(int)) || accum_is_stale(me->mcast_accum)) {
		st = sender_accum_write(me);
		if (FAILED(st))
			return st;
	}

	if (accum_is_empty(me->mcast_accum)) {
		st = accum_store(me->mcast_accum, &me->next_seq, sizeof(me->next_seq));
		if (FAILED(st))
			return st;
	}

	RECORD_LOCK(rec);
	record_set_seq(rec, me->next_seq);
	id = record_get_id(rec);
		
	st = accum_store(me->mcast_accum, &id, sizeof(id));
	if (FAILED(st)) {
		RECORD_UNLOCK(rec);
		return st;
	}

	st = accum_store(me->mcast_accum, record_get_val(rec), val_size);
	RECORD_UNLOCK(rec);
	return st;
}

static void* sender_mcast_proc(thread_handle thr)
{
	sender_handle me = thread_get_param(thr);
	size_t val_size = storage_get_val_size(me->store);
	status st = OK, st2;

	while (!thread_is_stopping(thr)) {
		record_handle rec;
		st = circ_remove(me->changed_q, (void**) &rec);
		if (st == BLOCKED) {
			st = OK;
			if (accum_is_stale(me->mcast_accum)) {
				st = sender_accum_write(me);
				if (FAILED(st))
					break;
			} else if ((time(NULL) - me->last_mcast_send) >= me->heartbeat) {
				long hb_seq = -1;
				if (FAILED(st = accum_store(me->mcast_accum, &hb_seq, sizeof(hb_seq))) ||
					FAILED(st = sender_accum_write(me)))
					break;

				continue;
			}

			yield();
			continue;
		} else if (FAILED(st))
			break;

		st = sender_mcast_on_write(me, rec, val_size);
		if (FAILED(st))
			break;
	}

	st2 = sock_close(me->mcast_sock);
	if (!FAILED(st))
		st = st2;

	thread_has_stopped(thr);
	return (void*) (long) st;
}

static status sender_tcp_close_proc(poll_handle poller, sock_handle sock, short* events, void* param)
{
	struct sender_tcp_req_param_t* req_param = sock_get_property(sock);
	status st;

	if (req_param) {
		xfree(req_param->send_buf);
		xfree(req_param);
	}

	st = sock_close(sock);
	sock_destroy(&sock);
	return st;
}

static status sender_tcp_on_accept(sender_handle me, sock_handle sock)
{
	sock_handle accepted;
	struct sender_tcp_req_param_t* req_param;

	status st = sock_accept(sock, &accepted);
	if (FAILED(st))
		return st;

	st = sock_write(accepted, me->hello_str, me->hello_len);
	if (FAILED(st))
		return st;

	req_param = XMALLOC(struct sender_tcp_req_param_t);
	if (!req_param) {
		sock_destroy(&accepted);
		return NO_MEMORY;
	}

	BZERO(req_param);

	req_param->val_size = storage_get_val_size(me->store);
	req_param->pkt_size = sizeof(long) + sizeof(int) + req_param->val_size;

	req_param->send_buf = xmalloc(req_param->pkt_size);
	if (!req_param->send_buf) {
		xfree(req_param);
		sock_destroy(&accepted);
		return NO_MEMORY;
	}

	req_param->send_seq = (long*) req_param->send_buf;
	req_param->send_id = (int*) (req_param->send_seq + 1);

	req_param->me = me;
	req_param->sock = accepted;
	req_param->last_tcp_send = time(NULL);

	sock_set_property(accepted, req_param);
	sock_nonblock(accepted);

	return poll_add(me->poller, accepted, POLLIN);
}

static status sender_tcp_on_hup(sender_handle me, sock_handle sock)
{
	status st = poll_remove(me->poller, sock);
	if (FAILED(st))
		return st;

	return sender_tcp_close_proc(me->poller, sock, NULL, NULL);
}

static status sender_tcp_on_write_remaining(struct sender_tcp_req_param_t* req_param)
{
	status st = OK;
	size_t sent_sz = 0;

	while (req_param->remain_send > 0) {
		st = sock_sendto(req_param->sock, req_param->next_send, req_param->remain_send);
		if (FAILED(st))
			break;

		req_param->next_send += st;
		req_param->remain_send -= st;
		sent_sz += st;
	}

	if (sent_sz > 0) {
		req_param->last_tcp_send = time(NULL);

		SPIN_LOCK(&req_param->me->stats.lock);
		req_param->me->stats.tcp_bytes_sent += sent_sz;
		SPIN_UNLOCK(&req_param->me->stats.lock);
	}

	return st;
}

static status sender_tcp_on_write_iter_func(record_handle rec, void* param)
{
	struct sender_tcp_req_param_t* req_param = param;
	status st;

	RECORD_LOCK(rec);
	*req_param->send_seq = record_get_seq(rec);

	if (*req_param->send_seq < req_param->min_store_seq_seen)
		req_param->min_store_seq_seen = *req_param->send_seq;

	if (*req_param->send_seq < req_param->range.low || *req_param->send_seq >= req_param->range.high) {
		RECORD_UNLOCK(rec);
		return TRUE;
	}

	*req_param->send_id = record_get_id(rec);
	memcpy(req_param->send_id + 1, record_get_val(rec), req_param->val_size);
	RECORD_UNLOCK(rec);

	req_param->curr_rec = rec;
	req_param->next_send = req_param->send_buf;
	req_param->remain_send = req_param->pkt_size;

	st = sender_tcp_on_write_remaining(req_param);
	return FAILED(st) ? st : TRUE;
}

static status sender_tcp_on_write(sender_handle me, sock_handle sock)
{
	struct sender_tcp_req_param_t* req_param = sock_get_property(sock);
	status st;

	if (req_param->remain_send > 0) {
		st = sender_tcp_on_write_remaining(req_param);
		if (st == BLOCKED)
			return OK;
		else if (st == CLOSED || st == TIMEDOUT)
			return sender_tcp_on_hup(me, sock);
		else if (FAILED(st))
			return st;
	}

	if (req_param->sending_hb) {
		req_param->sending_hb = FALSE;
		return poll_set_event(me->poller, sock, POLLIN);
	}

	st = storage_iterate(me->store, sender_tcp_on_write_iter_func, req_param->curr_rec, req_param);
	if (st == BLOCKED)
		return OK;
	else if (st == CLOSED || st == TIMEDOUT)
		return sender_tcp_on_hup(me, sock);
	else if (FAILED(st))
		return st;
	else if (st) {
		req_param->curr_rec = NULL;
		me->min_store_seq = req_param->min_store_seq_seen;
		st = poll_set_event(me->poller, sock, POLLIN);
	}

	return st;

}

static status sender_tcp_on_read(sender_handle me, sock_handle sock)
{
	struct sender_tcp_req_param_t* req_param = sock_get_property(sock);
	status st;

	char* p = (void*) &req_param->range;
	size_t sz = sizeof(req_param->range);

	while (sz > 0) {
		st = sock_read(sock, p, sz);
		if (st == CLOSED || st == TIMEDOUT)
			return sender_tcp_on_hup(me, sock);
		else if (st == BLOCKED) {
			yield();
			continue;
		} else if (FAILED(st))
			return st;

		p += st;
		sz -= st;
	}

	SPIN_LOCK(&me->stats.lock);
	me->stats.tcp_gap_count++;
	SPIN_UNLOCK(&me->stats.lock);

	if (req_param->range.low >= req_param->range.high) {
		errno = EPROTO;
		error_errno("sender_tcp_on_read");
		return BAD_PROTOCOL;
	}

	if (req_param->range.high <= me->min_store_seq)
		return OK;

	req_param->min_store_seq_seen = LONG_MAX;

	return poll_set_event(me->poller, sock, POLLOUT);
}

static status sender_tcp_event_proc(poll_handle poller, sock_handle sock, short* revents, void* param)
{
	sender_handle me = param;
	if (sock == me->listen_sock)
		return sender_tcp_on_accept(me, sock);

	if (*revents & POLLHUP)
		return sender_tcp_on_hup(me, sock);
	else if (*revents & POLLIN)
		return sender_tcp_on_read(me, sock);
	else if (*revents & POLLOUT)
		return sender_tcp_on_write(me, sock);

	return OK;
}

static status sender_tcp_check_heartbeat_proc(poll_handle poller, sock_handle sock, short* events, void* param)
{
	sender_handle me = param;
	struct sender_tcp_req_param_t* req_param = sock_get_property(sock);

	if (sock == me->listen_sock || (time(NULL) - req_param->last_tcp_send) < me->heartbeat)
		return OK;

	*req_param->send_seq = -1;

	req_param->next_send = req_param->send_buf;
	req_param->remain_send = sizeof(*req_param->send_seq);
	req_param->sending_hb = TRUE;

	return poll_set_event(poller, sock, POLLOUT);
}

static void* sender_tcp_proc(thread_handle thr)
{
	sender_handle me = thread_get_param(thr);
	status st = OK, st2;

	while (!thread_is_stopping(thr)) {
		st = poll_events(me->poller, 10);
		if (FAILED(st))
			break;

		if (st == 0) {
			st = poll_process(me->poller, sender_tcp_check_heartbeat_proc, (void*) me);
			if (FAILED(st))
				break;

			continue;
		}

		st = poll_process_events(me->poller, sender_tcp_event_proc, (void*) me);
		if (FAILED(st))
			break;
	}

	st2 = poll_process(me->poller, sender_tcp_close_proc, NULL);
	if (!FAILED(st))
		st = st2;

	thread_has_stopped(thr);
	return (void*) (long) st;
}

status sender_create(sender_handle* psend, storage_handle store, unsigned q_capacity, int hb_secs,
					 const char* mcast_addr, int mcast_port, int mcast_ttl,
					 const char* tcp_addr, int tcp_port)
{
	if (!psend || !mcast_addr || mcast_port < 0 || !tcp_addr || tcp_port < 0 || q_capacity == 0) {
		error_invalid_arg("sender_create");
		return FAIL;
	}

	*psend = XMALLOC(struct sender_t);
	if (!*psend)
		return NO_MEMORY;

	BZERO(*psend);

	SPIN_CREATE(&(*psend)->stats.lock);
	(*psend)->store = store;
	(*psend)->next_seq = 1;
	(*psend)->min_store_seq = 0;
	(*psend)->heartbeat = hb_secs;
	(*psend)->last_mcast_send = time(NULL);

	(*psend)->hello_len = sprintf((*psend)->hello_str, "%d\r\n%s\r\n%d\r\n%d\r\n%d\r\n%lu\r\n%d\r\n",
								  STORAGE_VERSION, mcast_addr, mcast_port,
								  storage_get_base_id(store), storage_get_max_id(store),
								  storage_get_val_size(store), (*psend)->heartbeat);

	if ((*psend)->hello_len < 0) {
		error_errno("sprintf");
		sender_destroy(psend);
		return FAIL;
	}

	if (FAILED(circ_create(&(*psend)->changed_q, q_capacity)) ||
		FAILED(accum_create(&(*psend)->mcast_accum, MTU_BYTES, MAX_AGE_USEC)) ||
		FAILED(sock_create(&(*psend)->mcast_sock, SOCK_DGRAM, mcast_addr, mcast_port)) ||
		FAILED(sock_mcast_bind((*psend)->mcast_sock)) ||
		FAILED(sock_mcast_set_ttl((*psend)->mcast_sock, mcast_ttl)) ||
		FAILED(sock_create(&(*psend)->listen_sock, SOCK_STREAM, tcp_addr, tcp_port)) ||
		FAILED(sock_listen((*psend)->listen_sock, 5)) ||
		FAILED(poll_create(&(*psend)->poller, 10)) ||
		FAILED(poll_add((*psend)->poller, (*psend)->listen_sock, POLLIN)) ||
		FAILED(thread_create(&(*psend)->tcp_thr, sender_tcp_proc, (void*) *psend)) ||
		FAILED(thread_create(&(*psend)->mcast_thr, sender_mcast_proc, (void*) *psend))) {
		sender_destroy(psend);
		return FAIL;
	}

	return OK;
}

void sender_destroy(sender_handle* psend)
{
	if (!psend || !*psend)
		return;

	error_save_last();

	thread_destroy(&(*psend)->mcast_thr);
	thread_destroy(&(*psend)->tcp_thr);
	sock_destroy(&(*psend)->listen_sock);
	sock_destroy(&(*psend)->mcast_sock);
	poll_destroy(&(*psend)->poller);
	circ_destroy(&(*psend)->changed_q);
	accum_destroy(&(*psend)->mcast_accum);

	error_restore_last();

	xfree(*psend);
	*psend = NULL;
}

status sender_record_changed(sender_handle send, record_handle rec)
{
	return circ_insert(send->changed_q, rec);
}

boolean sender_is_running(sender_handle send)
{
	return (send->tcp_thr && thread_is_running(send->tcp_thr)) && (send->mcast_thr && thread_is_running(send->mcast_thr));
}

status sender_stop(sender_handle send)
{
	void* p1;
	status st = thread_stop(send->mcast_thr, &p1);

	void* p2;
	status st2 = thread_stop(send->tcp_thr, &p2);

	if (!FAILED(st))
		st = (long) p1;

	if (!FAILED(st2))
		st2 = (long) p2;

	return FAILED(st) ? st : st2;
}

static long sender_get_stat(sender_handle me, long* pval)
{
	long n;
	SPIN_LOCK(&me->stats.lock);
	n = *pval;
	SPIN_UNLOCK(&me->stats.lock);
	return n;
}

long sender_get_tcp_gap_count(sender_handle send)
{
	return sender_get_stat(send, &send->stats.tcp_gap_count);
}

long sender_get_tcp_bytes_sent(sender_handle send)
{
	return sender_get_stat(send, &send->stats.tcp_bytes_sent);
}

long sender_get_mcast_bytes_sent(sender_handle send)
{
	return sender_get_stat(send, &send->stats.mcast_bytes_sent);
}
