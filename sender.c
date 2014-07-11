#include "sender.h"
#include "accum.h"
#include "error.h"
#include "poll.h"
#include "spin.h"
#include "thread.h"
#include "xalloc.h"
#include "yield.h"
#include <errno.h>
#include <limits.h>
#include <poll.h>
#include <stdio.h>
#include <time.h>
#include <sys/socket.h>

#define HEARTBEAT_SEQ -1
#define WILL_QUIT_SEQ -2
#define MAX_AGE_MILLISEC 10

struct sender_stats_t
{
	spin_lock_t lock;
	size_t tcp_gap_count;
	size_t tcp_bytes_sent;
	size_t mcast_bytes_sent;
};

struct sender_t
{
	thread_handle tcp_thr;
	sock_handle mcast_sock;
	sock_handle listen_sock;
	poll_handle poller;
	accum_handle mcast_accum;
	storage_handle store;
	size_t mcast_mtu;
	size_t val_size;
	long next_seq;
	long min_store_seq;
	time_t last_mcast_send;
	boolean conflate_pkt;
	spin_lock_t mcast_lock;
	int heartbeat_secs;
	struct sender_stats_t stats;
	size_t hello_len;
	char hello_str[128];
};

struct tcp_req_param_t
{
	sender_handle me;
	sock_handle sock;
	size_t val_size;
	size_t pkt_size;
	char* send_buf;
	long* send_seq;
	int* send_id;
	char* next_send;
	size_t remain_send;
	record_handle curr_rec;
	long min_store_seq_seen;
	struct storage_seq_range_t range;
	time_t last_tcp_send;
	boolean sending_hb;
};

static status write_accum(sender_handle me)
{
	const void* data;
	size_t sz;
	status st = accum_get_batched(me->mcast_accum, &data, &sz);
	if (FAILED(st))
		return st;
	else if (!st)
		return OK;

	st = sock_sendto(me->mcast_sock, data, sz);
	if (!FAILED(st)) {
		accum_clear(me->mcast_accum);
		me->next_seq++;

		me->last_mcast_send = time(NULL);

		SPIN_LOCK(&me->stats.lock);
		me->stats.mcast_bytes_sent += sz;
		SPIN_UNLOCK(&me->stats.lock);
	}

	return st;
}

static boolean mcast_accum_is_full(sender_handle me, record_handle rec)
{
	return accum_get_available(me->mcast_accum) < (me->val_size + sizeof(int)) &&
		(!me->conflate_pkt || record_get_sequence(rec) != me->next_seq);
}

static status mcast_on_write(sender_handle me, record_handle rec)
{
	status st = OK;
	void* stored_at;
	int id;

	if (((mcast_accum_is_full(me, rec) || accum_is_stale(me->mcast_accum)) && FAILED(st = write_accum(me))) ||
		(accum_is_empty(me->mcast_accum) && FAILED(st = accum_store(me->mcast_accum, &me->next_seq, sizeof(me->next_seq), NULL))))
		return st;

	id = record_get_id(rec);
	RECORD_LOCK(rec);

	if (me->conflate_pkt && record_get_sequence(rec) == me->next_seq)
		memcpy(record_get_conflated(rec), record_get_value(rec), me->val_size);
	else if (!FAILED(st = accum_store(me->mcast_accum, &id, sizeof(id), NULL)) &&
			 !FAILED(st = accum_store(me->mcast_accum, record_get_value(rec), me->val_size, &stored_at))) {
		record_set_sequence(rec, me->next_seq);
		if (me->conflate_pkt)
			record_set_conflated(rec, stored_at);
	}

	RECORD_UNLOCK(rec);
	return st;
}

static status tcp_close_func(poll_handle poller, sock_handle sock, short* events, void* param)
{
	struct tcp_req_param_t* req_param = sock_get_property(sock);
	status st;

	if (req_param) {
		xfree(req_param->send_buf);
		xfree(req_param);
	}

	st = sock_close(sock);
	sock_destroy(&sock);
	return st;
}

static status tcp_will_quit_func(poll_handle poller, sock_handle sock, short* events, void* param)
{
	long quit_seq = WILL_QUIT_SEQ;
	return sock_write(sock, &quit_seq, sizeof(quit_seq));
}

static status tcp_on_accept(sender_handle me, sock_handle sock)
{
	sock_handle accepted;
	struct tcp_req_param_t* req_param;
	status st;

	if (FAILED(st = sock_accept(sock, &accepted)) ||
		FAILED(st = sock_write(accepted, me->hello_str, me->hello_len)))
		return st;

	req_param = XMALLOC(struct tcp_req_param_t);
	if (!req_param) {
		sock_destroy(&accepted);
		return NO_MEMORY;
	}

	BZERO(req_param);

	req_param->val_size = storage_get_value_size(me->store);
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

static status tcp_on_hup(sender_handle me, sock_handle sock)
{
	status st = poll_remove(me->poller, sock);
	if (FAILED(st))
		return st;

	return tcp_close_func(me->poller, sock, NULL, NULL);
}

static status tcp_on_write_remaining(struct tcp_req_param_t* req_param)
{
	size_t sent_sz = 0;
	status st = OK;

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

static status tcp_on_write_iter_fn(record_handle rec, void* param)
{
	struct tcp_req_param_t* req_param = param;
	status st;

	RECORD_LOCK(rec);
	*req_param->send_seq = record_get_sequence(rec);

	if (*req_param->send_seq < req_param->min_store_seq_seen)
		req_param->min_store_seq_seen = *req_param->send_seq;

	if (*req_param->send_seq < req_param->range.low || *req_param->send_seq >= req_param->range.high) {
		RECORD_UNLOCK(rec);
		return TRUE;
	}

	*req_param->send_id = record_get_id(rec);
	memcpy(req_param->send_id + 1, record_get_value(rec), req_param->val_size);
	RECORD_UNLOCK(rec);

	req_param->curr_rec = rec;
	req_param->next_send = req_param->send_buf;
	req_param->remain_send = req_param->pkt_size;

	st = tcp_on_write_remaining(req_param);
	return FAILED(st) ? st : TRUE;
}

static status tcp_on_write(sender_handle me, sock_handle sock)
{
	struct tcp_req_param_t* req_param = sock_get_property(sock);
	status st;

	if (req_param->remain_send > 0) {
		st = tcp_on_write_remaining(req_param);
		if (st == BLOCKED)
			return OK;
		else if (st == CLOSED || st == TIMEDOUT)
			return tcp_on_hup(me, sock);
		else if (FAILED(st))
			return st;
	}

	if (req_param->sending_hb) {
		req_param->sending_hb = FALSE;
		return poll_set_event(me->poller, sock, POLLIN);
	}

	st = storage_iterate(me->store, tcp_on_write_iter_fn, req_param->curr_rec, req_param);
	if (st == BLOCKED)
		return OK;
	else if (st == CLOSED || st == TIMEDOUT)
		return tcp_on_hup(me, sock);
	else if (FAILED(st))
		return st;
	else if (st) {
		req_param->curr_rec = NULL;
		me->min_store_seq = req_param->min_store_seq_seen;
		st = poll_set_event(me->poller, sock, POLLIN);
	}

	return st;
}

static status tcp_on_read(sender_handle me, sock_handle sock)
{
	struct tcp_req_param_t* req_param = sock_get_property(sock);
	size_t gap_inc = 0;
	status st;

	req_param->range.low = LONG_MAX;
	req_param->range.high = LONG_MIN;

	for (;;) {
		struct storage_seq_range_t range;
		char* p = (void*) &range;
		size_t sz = sizeof(range);

		while (sz > 0) {
			if (thread_is_stopping(me->tcp_thr))
				return OK;

			st = sock_read(sock, p, sz);
			if (st == CLOSED || st == TIMEDOUT)
				return tcp_on_hup(me, sock);
			else if (st == BLOCKED) {
				if (sz == sizeof(range))
					goto read_all;

				snooze();
				continue;
			} else if (FAILED(st))
				return st;

			p += st;
			sz -= st;
		}

		if (range.low >= range.high) {
			errno = EPROTO;
			error_errno("tcp_on_read");
			return BAD_PROTOCOL;
		}

		gap_inc++;

		if (range.low < req_param->range.low)
			req_param->range.low = range.low;

		if (range.high > req_param->range.high)
			req_param->range.high = range.high;
	}

read_all:
	if (gap_inc == 0)
		return OK;

	SPIN_LOCK(&me->stats.lock);
	me->stats.tcp_gap_count += gap_inc;
	SPIN_UNLOCK(&me->stats.lock);

	if (req_param->range.high <= me->min_store_seq)
		return OK;

	req_param->min_store_seq_seen = LONG_MAX;
	return poll_set_event(me->poller, sock, POLLOUT);
}

static status tcp_event_func(poll_handle poller, sock_handle sock, short* revents, void* param)
{
	sender_handle me = param;
	if (sock == me->listen_sock)
		return tcp_on_accept(me, sock);

	if (*revents & POLLHUP)
		return tcp_on_hup(me, sock);
	else if (*revents & POLLIN)
		return tcp_on_read(me, sock);
	else if (*revents & POLLOUT)
		return tcp_on_write(me, sock);

	return OK;
}

static status mcast_check_heartbeat_or_stale(sender_handle me)
{
	status st = OK;
	if (SPIN_TRY_LOCK(&me->mcast_lock)) {
		if (accum_is_stale(me->mcast_accum))
			st = write_accum(me);
		else if ((time(NULL) - me->last_mcast_send) >= me->heartbeat_secs) {
			long hb_seq = HEARTBEAT_SEQ;
			if (!FAILED(st = accum_store(me->mcast_accum, &hb_seq, sizeof(hb_seq), NULL)))
				st = write_accum(me);
		}

		SPIN_UNLOCK(&me->mcast_lock);
	}

	return st;
}

static status tcp_check_heartbeat_func(poll_handle poller, sock_handle sock, short* events, void* param)
{
	sender_handle me = param;
	struct tcp_req_param_t* req_param = sock_get_property(sock);

	if (sock == me->listen_sock || *events & POLLOUT || (time(NULL) - req_param->last_tcp_send) < me->heartbeat_secs)
		return OK;

	*req_param->send_seq = HEARTBEAT_SEQ;
	req_param->remain_send = sizeof(*req_param->send_seq);
	req_param->next_send = req_param->send_buf;
	req_param->sending_hb = TRUE;

	return poll_set_event(poller, sock, POLLOUT);
}

static void* tcp_func(thread_handle thr)
{
	sender_handle me = thread_get_param(thr);
	status st = OK, st2;

	while (!thread_is_stopping(thr))
		if (FAILED(st = mcast_check_heartbeat_or_stale(me)) ||
			FAILED(st = poll_process(me->poller, tcp_check_heartbeat_func, (void*) me)) ||
			FAILED(st = poll_events(me->poller, MAX_AGE_MILLISEC)) ||
			(st > 0 && FAILED(st = poll_process_events(me->poller, tcp_event_func, (void*) me))))
			break;

	st2 = poll_remove(me->poller, me->listen_sock);
	if (!FAILED(st))
		st = st2;

	st2 = poll_process(me->poller, tcp_will_quit_func, NULL);
	if (!FAILED(st))
		st = st2;

	slumber(1);

	st2 = poll_process(me->poller, tcp_close_func, NULL);
	if (!FAILED(st))
		st = st2;

	thread_has_stopped(thr);
	return (void*) (long) st;
}

static status get_udp_mtu(sock_handle sock, const char* dest_ip, size_t* pmtu)
{
	char* device;
	status st;

	if (!FAILED(st = sock_get_interface(dest_ip, &device))) {
		if (!FAILED(st = sock_get_mtu(sock, device, pmtu)))
			*pmtu -= IP_OVERHEAD + UDP_OVERHEAD;

		xfree(device);
	}

	return st;
}

status sender_create(sender_handle* psend, storage_handle store, int hb_secs, boolean conflate_packet,
					 const char* mcast_addr, int mcast_port, int mcast_ttl, const char* tcp_addr, int tcp_port)
{
	status st;
	if (!psend || !store || hb_secs <= 0 || !mcast_addr || mcast_port < 0 || !tcp_addr || tcp_port < 0) {
		error_invalid_arg("sender_create");
		return FAIL;
	}

	if (!storage_is_segment_owner(store)) {
		errno = EPERM;
		error_errno("sender_create");
		return FAIL;
	}

	*psend = XMALLOC(struct sender_t);
	if (!*psend)
		return NO_MEMORY;

	BZERO(*psend);

	SPIN_CREATE(&(*psend)->mcast_lock);
	SPIN_CREATE(&(*psend)->stats.lock);

	(*psend)->store = store;
	(*psend)->val_size = storage_get_value_size(store);
	(*psend)->next_seq = 1;
	(*psend)->min_store_seq = 0;
	(*psend)->heartbeat_secs = hb_secs;
	(*psend)->last_mcast_send = time(NULL);
	(*psend)->conflate_pkt = conflate_packet;

	if (FAILED(st = sock_create(&(*psend)->mcast_sock, SOCK_DGRAM, mcast_addr, mcast_port)) ||
		FAILED(st = get_udp_mtu((*psend)->mcast_sock, mcast_addr, &(*psend)->mcast_mtu))) {
		sender_destroy(psend);
		return st;
	}

	(*psend)->hello_len = snprintf((*psend)->hello_str, sizeof((*psend)->hello_str),
								   "%d\r\n%s\r\n%d\r\n%lu\r\n%d\r\n%d\r\n%lu\r\n%d\r\n",
								   STORAGE_VERSION, mcast_addr, mcast_port, (*psend)->mcast_mtu,
								   storage_get_base_id(store), storage_get_max_id(store),
								   storage_get_value_size(store), (*psend)->heartbeat_secs);

	if ((*psend)->hello_len < 0) {
		error_errno("snprintf");
		sender_destroy(psend);
		return FAIL;
	}

	if ((*psend)->hello_len >= sizeof((*psend)->hello_str)) {
		errno = ENOBUFS;
		error_errno("snprintf");
		sender_destroy(psend);
		return FAIL;
	}

	if (FAILED(st = accum_create(&(*psend)->mcast_accum, (*psend)->mcast_mtu, MAX_AGE_MILLISEC * 1000)) ||
		FAILED(st = sock_mcast_bind((*psend)->mcast_sock)) ||
		FAILED(st = sock_mcast_set_ttl((*psend)->mcast_sock, mcast_ttl)) ||
		FAILED(st = sock_create(&(*psend)->listen_sock, SOCK_STREAM, tcp_addr, tcp_port)) ||
		FAILED(st = sock_listen((*psend)->listen_sock, 5)) ||
		FAILED(st = poll_create(&(*psend)->poller, 10)) ||
		FAILED(st = poll_add((*psend)->poller, (*psend)->listen_sock, POLLIN)) ||
		FAILED(st = thread_create(&(*psend)->tcp_thr, tcp_func, (void*) *psend))) {
		sender_destroy(psend);
		return st;
	}

	return OK;
}

void sender_destroy(sender_handle* psend)
{
	if (!psend || !*psend)
		return;

	error_save_last();

	thread_destroy(&(*psend)->tcp_thr);
	sock_destroy(&(*psend)->listen_sock);
	sock_destroy(&(*psend)->mcast_sock);
	poll_destroy(&(*psend)->poller);
	accum_destroy(&(*psend)->mcast_accum);

	error_restore_last();

	xfree(*psend);
	*psend = NULL;
}

status sender_record_changed(sender_handle send, record_handle rec)
{
	status st = OK;
	if (!rec) {
		error_invalid_arg("sender_record_changed");
		return FAIL;
	}

	if (poll_get_count(send->poller) > 1) {
		SPIN_LOCK(&send->mcast_lock);
		st = mcast_on_write(send, rec);
		SPIN_UNLOCK(&send->mcast_lock);
	}

	return st;
}

status sender_flush(sender_handle send)
{
	status st;
	SPIN_LOCK(&send->mcast_lock);
	st = write_accum(send);
	SPIN_UNLOCK(&send->mcast_lock);
	return st;
}

boolean sender_is_running(sender_handle send)
{
	return send->tcp_thr && thread_is_running(send->tcp_thr);
}

status sender_stop(sender_handle send)
{
	void* p;
	status st2, st = thread_stop(send->tcp_thr, &p);
	if (!FAILED(st))
		st = (long) p;

	st2 = sock_close(send->listen_sock);
	if (!FAILED(st))
		st = st2;

	st2 = sock_close(send->mcast_sock);
	if (!FAILED(st))
		st = st2;

	return st;
}

static size_t get_stat(sender_handle me, const size_t* pval)
{
	size_t n;
	SPIN_LOCK(&me->stats.lock);
	n = *pval;
	SPIN_UNLOCK(&me->stats.lock);
	return n;
}

size_t sender_get_tcp_gap_count(sender_handle send)
{
	return get_stat(send, &send->stats.tcp_gap_count);
}

size_t sender_get_tcp_bytes_sent(sender_handle send)
{
	return get_stat(send, &send->stats.tcp_bytes_sent);
}

size_t sender_get_mcast_bytes_sent(sender_handle send)
{
	return get_stat(send, &send->stats.mcast_bytes_sent);
}

int sender_get_subscriber_count(sender_handle send)
{
	return poll_get_count(send->poller) - 1;
}
