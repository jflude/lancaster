#include "receiver.h"
#include "circ.h"
#include "error.h"
#include "sock.h"
#include "spin.h"
#include "thread.h"
#include "xalloc.h"
#include <alloca.h>
#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <time.h>
#include <sys/socket.h>

struct receiver_stats_t
{
	spin_lock_t lock;
	long tcp_gap_count;
	long tcp_bytes_recv;
	long mcast_bytes_recv;
};

struct receiver_t
{
	thread_handle mcast_thr;
	thread_handle tcp_thr;
	sock_handle mcast_sock;
	sock_handle tcp_sock;
	circ_handle changed_q;
	circ_handle tcp_q;
	storage_handle store;
	long next_seq;
	time_t last_mcast_recv;
	time_t last_tcp_recv;
	int heartbeat;
	struct receiver_stats_t stats;
};

static void receiver_circ_insert(thread_handle thr, circ_handle queue, record_handle rec)
{
	while (!thread_is_stopping(thr)) {
		if (circ_insert(queue, rec) != BLOCKED)
			break;

		yield();
	}
}

static void receiver_copy_queue(receiver_handle me)
{
	while (!thread_is_stopping(me->mcast_thr)) {
		record_handle rec;
		if (circ_remove(me->tcp_q, (void**) &rec) == BLOCKED)
			break;

		receiver_circ_insert(me->mcast_thr, me->changed_q, rec);
	}
}

static void* receiver_mcast_proc(thread_handle thr)
{
	receiver_handle me = thread_get_param(thr);
	size_t val_size = storage_get_val_size(me->store);
	status st = OK, st2;

	while (!thread_is_stopping(thr)) {
		char buf[MTU_BYTES];
		const char *p, *last;
		long* recv_seq = (long*) buf;

		st = sock_recvfrom(me->mcast_sock, buf, sizeof(buf));
		if (st == BLOCKED) {
			st = OK;
			receiver_copy_queue(me);

			if ((time(NULL) - me->last_mcast_recv) > me->heartbeat) {
				error_heartbeat("receiver_mcast_proc");
				st = HEARTBEAT;
				break;
			}

			yield();
			continue;
		} else if (FAILED(st))
			break;

		SPIN_LOCK(&me->stats.lock);
		me->stats.mcast_bytes_recv += st;
		SPIN_UNLOCK(&me->stats.lock);

		me->last_mcast_recv = time(NULL);

		if (st < sizeof(*recv_seq)) {
			errno = EPROTO;
			error_errno("receiver_mcast_proc");
			st = BAD_PROTOCOL;
			break;
		}

		if (*recv_seq < me->next_seq)
			continue;

		last = buf + st;
		for (p = buf + sizeof(*recv_seq); p < last; p += val_size + sizeof(int)) {
			record_handle rec;
			int* id = (int*) p;

			st = storage_lookup(me->store, *id, &rec);
			if (FAILED(st))
				goto finish;

			RECORD_LOCK(rec);
			memcpy(record_get_val(rec), id + 1, val_size);
			record_set_seq(rec, *recv_seq);
			RECORD_UNLOCK(rec);

			receiver_circ_insert(me->mcast_thr, me->changed_q, rec);
		}

		if (*recv_seq > me->next_seq) {
			struct storage_seq_range_t range;
			char* p = (void*) &range;
			size_t sz = sizeof(range);

			range.low = me->next_seq;
			range.high = me->next_seq = *recv_seq;

			while (sz > 0) {
				if (thread_is_stopping(thr))
					goto finish;

				st = sock_write(me->tcp_sock, p, sz);
				if (st == BLOCKED) {
					st = OK;
					yield();
					continue;
				} else if (FAILED(st))
					goto finish;

				p += st;
				sz -= st;
			}

			SPIN_LOCK(&me->stats.lock);
			me->stats.tcp_gap_count++;
			SPIN_UNLOCK(&me->stats.lock);
		}

		me->next_seq++;
		receiver_copy_queue(me);
	}

finish:
	st2 = sock_close(me->mcast_sock);
	if (!FAILED(st))
		st = st2;

	sock_destroy(&me->mcast_sock);

	thread_has_stopped(thr);
	return (void*) (long) st;
}

static void receiver_incr_tcp_bytes_recv(receiver_handle me, size_t n)
{
	if (n > 0) {
		SPIN_LOCK(&me->stats.lock);
		me->stats.tcp_bytes_recv += n;
		SPIN_UNLOCK(&me->stats.lock);
	}
}

static status receiver_tcp_read(thread_handle thr, char* buf, size_t sz)
{
	receiver_handle me = thread_get_param(thr);
	status st = TRUE;
	size_t bytes_in = 0;

	while (sz > 0) {
		if (thread_is_stopping(thr)) {
			st = FALSE;
			break;
		}

		st = sock_read(me->tcp_sock, buf, sz);
		if (st == BLOCKED) {
			st = OK;
			if ((time(NULL) - me->last_tcp_recv) > me->heartbeat) {
				error_heartbeat("receiver_tcp_proc");
				st = HEARTBEAT;
				break;
			}

			yield();
			continue;
		} else if (FAILED(st))
			break;

		buf += st;
		sz -= st;
		bytes_in += st;

		st = TRUE;
	}

	receiver_incr_tcp_bytes_recv(me, bytes_in);
	me->last_tcp_recv = time(NULL);
	return st;
}

static void* receiver_tcp_proc(thread_handle thr)
{
	receiver_handle me = thread_get_param(thr);
	size_t val_size = storage_get_val_size(me->store);
	size_t pkt_size = sizeof(long) + sizeof(int) + val_size;
	char* buf = alloca(pkt_size);

	long* recv_seq = (long*) buf;
	int* id = (int*) (recv_seq + 1);
	status st = OK, st2;

	while (!thread_is_stopping(thr)) {
		record_handle rec;
		st = receiver_tcp_read(thr, buf, sizeof(*recv_seq));
		if (FAILED(st) || !st)
			break;

		if (*recv_seq == -1)
			continue;

		st = receiver_tcp_read(thr, buf + sizeof(*recv_seq), pkt_size - sizeof(*recv_seq));
		if (FAILED(st) || !st || FAILED(st = storage_lookup(me->store, *id, &rec)))
			break;

		RECORD_LOCK(rec);
		if (*recv_seq <= record_get_seq(rec)) {
			RECORD_UNLOCK(rec);
			continue;
		}

		memcpy(record_get_val(rec), id + 1, val_size);
		record_set_seq(rec, *recv_seq);
		RECORD_UNLOCK(rec);

		receiver_circ_insert(me->tcp_thr, me->tcp_q, rec);
	}

	st2 = sock_close(me->tcp_sock);
	if (!FAILED(st))
		st = st2;

	sock_destroy(&me->tcp_sock);

	thread_has_stopped(thr);
	return (void*) (long) st;
}

status receiver_create(receiver_handle* precv, unsigned q_capacity, const char* tcp_addr, int tcp_port)
{
	char buf[128], mcast_addr[32];
	int proto_ver, mcast_port, base_id, max_id, hb_secs;
	size_t val_size;
	status st;

	if (!precv || !tcp_addr || tcp_port < 0 || q_capacity == 0) {
		error_invalid_arg("receiver_create");
		return FAIL;
	}

	*precv = XMALLOC(struct receiver_t);
	if (!*precv)
		return NO_MEMORY;

	BZERO(*precv);

	if (FAILED(st = sock_create(&(*precv)->tcp_sock, SOCK_STREAM, tcp_addr, tcp_port)) ||
		FAILED(st = sock_connect((*precv)->tcp_sock)) ||
		FAILED(st = sock_read((*precv)->tcp_sock, buf, sizeof(buf) - 1))) {
		receiver_destroy(precv);
		return st;
	}

	buf[st] = '\0';
	st = sscanf(buf, "%d %31s %d %d %d %lu %d", &proto_ver, mcast_addr, &mcast_port, &base_id, &max_id, &val_size, &hb_secs);
	if (st == EOF) {
		errno = EPROTO;
		error_errno("receiver_create");
		return BAD_PROTOCOL;
	}	

	if (st != 7 || proto_ver != STORAGE_VERSION) {
		errno = EPROTONOSUPPORT;
		error_errno("receiver_create");
		return BAD_PROTOCOL;
	}

	SPIN_CREATE(&(*precv)->stats.lock);
	(*precv)->next_seq = 1;
	(*precv)->heartbeat = 2 * hb_secs + 1;
	(*precv)->last_tcp_recv = (*precv)->last_mcast_recv = time(NULL);

	if (FAILED(st = storage_create(&(*precv)->store, base_id, max_id, val_size)) ||
		FAILED(st = circ_create(&(*precv)->changed_q, q_capacity)) ||
		FAILED(st = circ_create(&(*precv)->tcp_q, q_capacity)) ||
	    FAILED(st = sock_create(&(*precv)->mcast_sock, SOCK_DGRAM, mcast_addr, mcast_port)) ||
		FAILED(st = sock_mcast_bind((*precv)->mcast_sock)) ||
		FAILED(st = sock_nonblock((*precv)->mcast_sock)) ||
		FAILED(st = sock_nonblock((*precv)->tcp_sock)) ||
		FAILED(st = thread_create(&(*precv)->tcp_thr, receiver_tcp_proc, (void*) *precv)) ||
		FAILED(st = thread_create(&(*precv)->mcast_thr, receiver_mcast_proc, (void*) *precv))) {
		receiver_destroy(precv);
		return st;
	}

	return OK;
}

void receiver_destroy(receiver_handle* precv)
{
	if (!precv || !*precv)
		return;

	error_save_last();

	thread_destroy(&(*precv)->mcast_thr);
	thread_destroy(&(*precv)->tcp_thr);
	sock_destroy(&(*precv)->mcast_sock);
	sock_destroy(&(*precv)->tcp_sock);
	circ_destroy(&(*precv)->tcp_q);
	circ_destroy(&(*precv)->changed_q);
	storage_destroy(&(*precv)->store);

	error_restore_last();

	xfree(*precv);
	*precv = NULL;
}

storage_handle receiver_get_storage(receiver_handle recv)
{
	return recv->store;
}

status receiver_record_changed(receiver_handle recv, record_handle* prec)
{
	return circ_remove(recv->changed_q, (void**) prec);
}

boolean receiver_is_running(receiver_handle recv)
{
	return (recv->tcp_thr && thread_is_running(recv->tcp_thr)) && (recv->mcast_thr && thread_is_running(recv->mcast_thr));
}

status receiver_stop(receiver_handle recv)
{
	void* p1;
	status st = thread_stop(recv->mcast_thr, &p1);

	void* p2;
	status st2 = thread_stop(recv->tcp_thr, &p2);

	if (!FAILED(st))
		st = (long) p1;

	if (!FAILED(st2))
		st2 = (long) p2;

	return FAILED(st) ? st : st2;
}

static long receiver_get_stat(receiver_handle me, long* pval)
{
	long n;
	SPIN_LOCK(&me->stats.lock);
	n = *pval;
	SPIN_UNLOCK(&me->stats.lock);
	return n;
}

long receiver_get_tcp_gap_count(receiver_handle recv)
{
	return receiver_get_stat(recv, &recv->stats.tcp_gap_count);
}

long receiver_get_tcp_bytes_recv(receiver_handle recv)
{
	return receiver_get_stat(recv, &recv->stats.tcp_bytes_recv);
}

long receiver_get_mcast_bytes_recv(receiver_handle recv)
{
	return receiver_get_stat(recv, &recv->stats.mcast_bytes_recv);
}
