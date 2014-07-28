#include "sender.h"
#include "accum.h"
#include "error.h"
#include "poll.h"
#include "spin.h"
#include "thread.h"
#include "xalloc.h"
#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <sys/socket.h>

#define HEARTBEAT_SEQ -1
#define WILL_QUIT_SEQ -2

struct sender_stats_t
{
	spin_lock_t lock;
	size_t tcp_gap_count;
	size_t tcp_bytes_sent;
	size_t mcast_bytes_sent;
	size_t mcast_packets_sent;
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
	sequence next_seq;
	sequence min_store_seq;
	time_t last_mcast_send;
	boolean conflate_pkt;
	spin_lock_t mcast_lock;
	int heartbeat_sec;
	long max_age_sec;
	long max_age_nsec;
	void* time_stored_at;
	struct sender_stats_t stats;
	int hello_len;
	char hello_str[128];
};

struct tcp_req_param_t
{
	sender_handle me;
	sock_handle sock;
	size_t val_size;
	size_t pkt_size;
	char* send_buf;
	sequence* send_seq;
	identifier* send_id;
	char* next_send;
	size_t remain_send;
	record_handle curr_rec;
	sequence min_store_seq_seen;
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

#ifdef _POSIX_TIMERS
	if (clock_gettime(CLOCK_REALTIME, me->time_stored_at) == -1) {
		error_errno("clock_gettime");
		return FAIL;
	}
#else
	memset(me->time_stored_at, 0, sizeof(struct timespec));
#endif

	if (FAILED(st = sock_sendto(me->mcast_sock, data, sz)))
		return st;

	me->last_mcast_send = time(NULL);
	storage_set_send_recv_time(me->store, me->last_mcast_send);

	SPIN_LOCK(&me->stats.lock);
	me->stats.mcast_bytes_sent += sz;
	me->stats.mcast_packets_sent++;
	SPIN_UNLOCK(&me->stats.lock);

	accum_clear(me->mcast_accum);

	if (++me->next_seq < 0) {
		errno = EOVERFLOW;
		error_errno("write_accum");
		return FAIL;
	}

	return st;
}

static status mcast_on_write(sender_handle me, record_handle rec)
{
	status st = OK;
	if ((((accum_get_available(me->mcast_accum) < (me->val_size + sizeof(identifier)) &&
		   (!me->conflate_pkt || record_get_sequence(rec) != me->next_seq)) ||
		  accum_is_stale(me->mcast_accum)) && FAILED(st = write_accum(me))) ||
		(accum_is_empty(me->mcast_accum) &&
		 (FAILED(st = accum_store(me->mcast_accum, &me->next_seq, sizeof(me->next_seq), NULL)) ||
		  FAILED(st = accum_store(me->mcast_accum, NULL, sizeof(struct timespec), &me->time_stored_at)))))
		return st;

	if (me->conflate_pkt && record_get_sequence(rec) == me->next_seq)
		memcpy(record_get_conflated(rec), record_get_value(rec), me->val_size);
	else {
		identifier id = storage_get_id(me->store, rec);
		sequence seq = record_write_lock(rec);

		if (!FAILED(st = accum_store(me->mcast_accum, &id, sizeof(id), NULL))) {
			void* stored_at;
			if (!FAILED(st = accum_store(me->mcast_accum, record_get_value(rec), me->val_size, &stored_at))) {
				seq = me->next_seq;
				if (me->conflate_pkt)
					record_set_conflated(rec, stored_at);
			}
		}

		record_set_sequence(rec, seq);
	}

	return st;
}

static status tcp_close_func(poll_handle poller, sock_handle sock, short* events, void* param)
{
	struct tcp_req_param_t* req_param = sock_get_property(sock);
	status st;
	(void) poller; (void) events; (void) param;

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
	sequence quit_seq = WILL_QUIT_SEQ;
	(void) poller; (void) events; (void) param;
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
	req_param->pkt_size = sizeof(sequence) + sizeof(identifier) + req_param->val_size;

	req_param->send_buf = xmalloc(req_param->pkt_size);
	if (!req_param->send_buf) {
		xfree(req_param);
		sock_destroy(&accepted);
		return NO_MEMORY;
	}

	req_param->send_seq = (sequence*) req_param->send_buf;
	req_param->send_id = (identifier*) (req_param->send_seq + 1);

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
		if (FAILED(st = sock_sendto(req_param->sock, req_param->next_send, req_param->remain_send)))
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

	*req_param->send_seq = record_write_lock(rec);

	if (*req_param->send_seq < req_param->min_store_seq_seen)
		req_param->min_store_seq_seen = *req_param->send_seq;

	if (*req_param->send_seq < req_param->range.low || *req_param->send_seq >= req_param->range.high) {
		record_set_sequence(rec, *req_param->send_seq);
		return TRUE;
	}

	memcpy(req_param->send_id + 1, record_get_value(rec), req_param->val_size);
	record_set_sequence(rec, *req_param->send_seq);

	*req_param->send_id = storage_get_id(req_param->me->store, rec);
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
		else if (st == EOF || st == TIMEDOUT)
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
	else if (st == EOF || st == TIMEDOUT)
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

	req_param->range.low = SEQUENCE_MAX;
	req_param->range.high = SEQUENCE_MIN;

	for (;;) {
		struct storage_seq_range_t range;
		char* p = (void*) &range;
		size_t sz = sizeof(range);

		while (sz > 0) {
			if (thread_is_stopping(me->tcp_thr))
				return OK;

			st = sock_read(sock, p, sz);
			if (st == EOF || st == TIMEDOUT)
				return tcp_on_hup(me, sock);
			else if (st == BLOCKED) {
				if (sz == sizeof(range))
					goto read_all;

				snooze(0, 1000);
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

	req_param->min_store_seq_seen = SEQUENCE_MAX;
	return poll_set_event(me->poller, sock, POLLOUT);
}

static status tcp_event_func(poll_handle poller, sock_handle sock, short* revents, void* param)
{
	sender_handle me = param;
	(void) poller;
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
	SPIN_LOCK(&me->mcast_lock);

	if (accum_is_stale(me->mcast_accum))
		st = write_accum(me);
	else if (poll_get_count(me->poller) > 1 && (time(NULL) - me->last_mcast_send) >= me->heartbeat_sec) {
		sequence hb_seq = HEARTBEAT_SEQ;
		if (!FAILED(st = accum_store(me->mcast_accum, &hb_seq, sizeof(hb_seq), NULL)) &&
			!FAILED(st = accum_store(me->mcast_accum, NULL, sizeof(struct timespec), &me->time_stored_at)))
			st = write_accum(me);
	}

	SPIN_UNLOCK(&me->mcast_lock);
	return st;
}

static status tcp_check_heartbeat_func(poll_handle poller, sock_handle sock, short* events, void* param)
{
	sender_handle me = param;
	struct tcp_req_param_t* req_param = sock_get_property(sock);

	if (sock == me->listen_sock || *events & POLLOUT || (time(NULL) - req_param->last_tcp_send) < me->heartbeat_sec)
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

	while (!thread_is_stopping(thr)) {
		if (FAILED(st = mcast_check_heartbeat_or_stale(me)) ||
			FAILED(st = poll_process(me->poller, tcp_check_heartbeat_func, me)) ||
			FAILED(st = poll_events(me->poller, 0)) ||
			(st > 0 && FAILED(st = poll_process_events(me->poller, tcp_event_func, me))))
			break;

		snooze(me->max_age_sec, me->max_age_nsec);
	}

	st2 = poll_remove(me->poller, me->listen_sock);
	if (!FAILED(st))
		st = st2;

	st2 = poll_process(me->poller, tcp_will_quit_func, NULL);
	if (!FAILED(st))
		st = st2;

	snooze(1, 0);

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

status sender_create(sender_handle* psend, storage_handle store, int hb_sec, long max_age_usec, boolean conflate_packet,
					 const char* mcast_addr, int mcast_port, int mcast_ttl, const char* tcp_addr, int tcp_port)
{
	status st;
	if (!psend || !store || hb_sec <= 0 || max_age_usec < 0 || !mcast_addr || mcast_port < 0 || !tcp_addr || tcp_port < 0) {
		error_invalid_arg("sender_create");
		return FAIL;
	}

	if (!storage_is_owner(store)) {
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
	(*psend)->max_age_sec = max_age_usec / 1000000;
	(*psend)->max_age_nsec = 1000 * (max_age_usec - 1000000 * (*psend)->max_age_sec);
	(*psend)->heartbeat_sec = hb_sec;
	(*psend)->last_mcast_send = time(NULL);
	(*psend)->conflate_pkt = conflate_packet;

	if (FAILED(st = sock_create(&(*psend)->mcast_sock, SOCK_DGRAM, mcast_addr, mcast_port)) ||
		FAILED(st = get_udp_mtu((*psend)->mcast_sock, mcast_addr, &(*psend)->mcast_mtu))) {
		sender_destroy(psend);
		return st;
	}

	(*psend)->hello_len = sprintf((*psend)->hello_str, "%d\r\n%s\r\n%d\r\n%lu\r\n%ld\r\n%ld\r\n%lu\r\n%ld\r\n%d\r\n",
								  STORAGE_VERSION, mcast_addr, mcast_port, (*psend)->mcast_mtu,
								  (long) storage_get_base_id(store), (long) storage_get_max_id(store),
								  storage_get_value_size(store), max_age_usec, (*psend)->heartbeat_sec);

	if ((*psend)->hello_len < 0) {
		error_errno("sprintf");
		sender_destroy(psend);
		return FAIL;
	}

	if (FAILED(st = accum_create(&(*psend)->mcast_accum, (*psend)->mcast_mtu, max_age_usec)) ||
		FAILED(st = sock_mcast_bind((*psend)->mcast_sock)) ||
		FAILED(st = sock_mcast_set_ttl((*psend)->mcast_sock, mcast_ttl)) ||
		FAILED(st = sock_create(&(*psend)->listen_sock, SOCK_STREAM, tcp_addr, tcp_port)) ||
		FAILED(st = sock_listen((*psend)->listen_sock, 5)) ||
		FAILED(st = poll_create(&(*psend)->poller, 10)) ||
		FAILED(st = poll_add((*psend)->poller, (*psend)->listen_sock, POLLIN)) ||
		FAILED(st = thread_create(&(*psend)->tcp_thr, tcp_func, *psend))) {
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

size_t sender_get_mcast_packets_sent(sender_handle send)
{
	return get_stat(send, &send->stats.mcast_packets_sent);
}

size_t sender_get_subscriber_count(sender_handle send)
{
	return poll_get_count(send->poller) - 1;
}
