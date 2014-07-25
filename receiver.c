#include "receiver.h"
#include "error.h"
#include "sock.h"
#include "spin.h"
#include "thread.h"
#include "xalloc.h"
#include <alloca.h>
#include <errno.h>
#include <float.h>
#include <math.h>
#include <poll.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <sys/socket.h>

#define HEARTBEAT_SEQ -1
#define WILL_QUIT_SEQ -2

struct receiver_stats_t
{
	spin_lock_t lock;
	size_t tcp_gap_count;
	size_t tcp_bytes_recv;
	size_t mcast_bytes_recv;
	size_t mcast_packets_recv;
	double mcast_min_latency;
	double mcast_max_latency;
	double mcast_mean_latency;
	double mcast_M2_latency;
};

struct receiver_t
{
	thread_handle mcast_thr;
	thread_handle tcp_thr;
	sock_handle mcast_sock;
	sock_handle tcp_sock;
	storage_handle store;
	size_t mcast_mtu;
	sequence next_seq;
	time_t last_mcast_recv;
	time_t last_tcp_recv;
	int heartbeat_secs;
	struct receiver_stats_t stats;
};

static void* mcast_func(thread_handle thr)
{
	receiver_handle me = thread_get_param(thr);
	size_t val_size = storage_get_value_size(me->store);
	char* buf = alloca(me->mcast_mtu);
	sequence* recv_seq = (sequence*) buf;
	struct timespec* recv_stamp = (struct timespec*) (recv_seq + 1);
	status st = OK, st2;

	while (!thread_is_stopping(thr)) {
		const char *p, *last;
#ifdef _POSIX_TIMERS
		struct timespec now;
		double latency, delta;
#endif
		st = sock_recvfrom(me->mcast_sock, buf, me->mcast_mtu);
		if (st == BLOCKED) {
			if ((time(NULL) - me->last_mcast_recv) > me->heartbeat_secs) {
				error_heartbeat("mcast_func");
				st = HEARTBEAT;
				break;
			}

			snooze();
			continue;
		} else if (FAILED(st))
			break;

#ifdef _POSIX_TIMERS
		if (clock_gettime(CLOCK_REALTIME, &now) == -1) {
			error_errno("clock_gettime");
			st = FAIL;
			break;
		}
#endif
		if (!sock_is_same_address(me->mcast_sock, me->tcp_sock)) {
			errno = EEXIST;
			error_errno("mcast_func");
			st = FAIL;
			break;
		}

		me->last_mcast_recv = time(NULL);

		SPIN_LOCK(&me->stats.lock);
		me->stats.mcast_bytes_recv += st;
		me->stats.mcast_packets_recv++;

		if ((unsigned) st < (sizeof(*recv_seq) + sizeof(*recv_stamp))) {
			SPIN_UNLOCK(&me->stats.lock);
			errno = EPROTO;
			error_errno("mcast_func");
			st = BAD_PROTOCOL;
			break;
		}

#ifdef _POSIX_TIMERS
		latency = 1000000000 * (now.tv_sec - recv_stamp->tv_sec) + now.tv_nsec - recv_stamp->tv_nsec;

		delta = latency - me->stats.mcast_mean_latency;
		me->stats.mcast_mean_latency += delta / me->stats.mcast_packets_recv;
		me->stats.mcast_M2_latency += delta * (latency - me->stats.mcast_mean_latency);

		if (latency < me->stats.mcast_min_latency)
			me->stats.mcast_min_latency = latency;

		if (latency > me->stats.mcast_max_latency)
			me->stats.mcast_max_latency = latency;
#endif
		SPIN_UNLOCK(&me->stats.lock);
		if (*recv_seq < me->next_seq)
			continue;

		last = buf + st;
		for (p = buf + sizeof(*recv_seq) + sizeof(*recv_stamp); p < last; p += val_size + sizeof(identifier)) {
			record_handle rec;
			identifier* id = (identifier*) p;

			if (FAILED(st = storage_lookup(me->store, *id, &rec)))
				goto finish;

			record_write_lock(rec);
			memcpy(record_get_value(rec), id + 1, val_size);
			record_set_sequence(rec, *recv_seq);

			if (FAILED(st = storage_write_queue(me->store, *id)))
				goto finish;
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
					snooze();
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
	}

finish:
	st2 = sock_close(me->mcast_sock);
	if (!FAILED(st))
		st = st2;

	sock_destroy(&me->mcast_sock);

	thread_has_stopped(thr);
	return (void*) (long) st;
}

static status tcp_read(receiver_handle me, char* buf, size_t sz)
{
	status st = TRUE;
	size_t bytes_in = 0;

	while (sz > 0) {
		if (thread_is_stopping(me->tcp_thr)) {
			st = FALSE;
			break;
		}

		st = sock_read(me->tcp_sock, buf, sz);
		if (st == BLOCKED) {
			st = OK;
			if ((time(NULL) - me->last_tcp_recv) > me->heartbeat_secs) {
				error_heartbeat("tcp_func");
				st = HEARTBEAT;
				break;
			}

			snooze();
			continue;
		} else if (FAILED(st))
			break;

		buf += st;
		sz -= st;
		bytes_in += st;

		st = TRUE;
	}

	if (bytes_in > 0) {
		me->last_tcp_recv = time(NULL);

		SPIN_LOCK(&me->stats.lock);
		me->stats.tcp_bytes_recv += bytes_in;
		SPIN_UNLOCK(&me->stats.lock);
	}

	return st;
}

static void* tcp_func(thread_handle thr)
{
	receiver_handle me = thread_get_param(thr);
	size_t val_size = storage_get_value_size(me->store);
	size_t pkt_size = sizeof(sequence) + sizeof(identifier) + val_size;
	char* buf = alloca(pkt_size);

	sequence* recv_seq = (sequence*) buf;
	identifier* id = (identifier*) (recv_seq + 1);
	status st = OK, st2;

	while (!thread_is_stopping(thr)) {
		record_handle rec;
		sequence seq;
		st = tcp_read(me, buf, sizeof(*recv_seq));
		if (FAILED(st) || !st)
			break;

		if (*recv_seq == HEARTBEAT_SEQ)
			continue;

		if (*recv_seq == WILL_QUIT_SEQ)
			break;

		st = tcp_read(me, buf + sizeof(*recv_seq), pkt_size - sizeof(*recv_seq));
		if (FAILED(st) || !st || FAILED(st = storage_lookup(me->store, *id, &rec)))
			break;

		seq = record_write_lock(rec);
		if (*recv_seq <= seq) {
			record_set_sequence(rec, seq);
			continue;
		}

		memcpy(record_get_value(rec), id + 1, val_size);
		record_set_sequence(rec, *recv_seq);

		if (FAILED(st = storage_write_queue(me->store, *id)))
			break;
	}

	st2 = sock_close(me->tcp_sock);
	if (!FAILED(st))
		st = st2;

	sock_destroy(&me->tcp_sock);

	thread_has_stopped(thr);
	return (void*) (long) st;
}

status receiver_create(receiver_handle* precv, const char* mmap_file, unsigned q_capacity, const char* tcp_addr, int tcp_port)
{
	char buf[128], mcast_addr[32];
	int proto_ver, mcast_port, hb_secs;
	long base_id, max_id;
	size_t val_size;
	status st;

	if (!precv || !tcp_addr || tcp_port < 0) {
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
	st = sscanf(buf, "%d %31s %d %lu %ld %ld %lu %d",
				&proto_ver, mcast_addr, &mcast_port, &(*precv)->mcast_mtu, &base_id, &max_id, &val_size, &hb_secs);

	if (st == EOF) {
		errno = EPROTO;
		error_errno("receiver_create");
		return BAD_PROTOCOL;
	}	

	if (st != 8 || proto_ver != STORAGE_VERSION) {
		errno = EPROTONOSUPPORT;
		error_errno("receiver_create");
		return BAD_PROTOCOL;
	}

	SPIN_CREATE(&(*precv)->stats.lock);

	(*precv)->next_seq = 1;
	(*precv)->heartbeat_secs = 2 * hb_secs + 1;
	(*precv)->stats.mcast_min_latency = DBL_MAX;
	(*precv)->stats.mcast_max_latency = DBL_MIN;
	(*precv)->last_tcp_recv = (*precv)->last_mcast_recv = time(NULL);

	if (FAILED(st = storage_create(&(*precv)->store, mmap_file, q_capacity, base_id, max_id, val_size)) ||
		FAILED(st = storage_reset((*precv)->store)) ||
	    FAILED(st = sock_create(&(*precv)->mcast_sock, SOCK_DGRAM, mcast_addr, mcast_port)) ||
		FAILED(st = sock_mcast_bind((*precv)->mcast_sock)) ||
		FAILED(st = sock_nonblock((*precv)->mcast_sock)) ||
		FAILED(st = sock_nonblock((*precv)->tcp_sock)) ||
		FAILED(st = thread_create(&(*precv)->tcp_thr, tcp_func, *precv)) ||
		FAILED(st = thread_create(&(*precv)->mcast_thr, mcast_func, *precv))) {
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
	storage_destroy(&(*precv)->store);

	error_restore_last();

	xfree(*precv);
	*precv = NULL;
}

storage_handle receiver_get_storage(receiver_handle recv)
{
	return recv->store;
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

static size_t get_long_stat(receiver_handle me, const size_t* pval)
{
	size_t n;
	SPIN_LOCK(&me->stats.lock);
	n = *pval;
	SPIN_UNLOCK(&me->stats.lock);
	return n;
}

static size_t get_double_stat(receiver_handle me, const double* pval)
{
	double n;
	SPIN_LOCK(&me->stats.lock);
	n = *pval;
	SPIN_UNLOCK(&me->stats.lock);
	return n;
}

size_t receiver_get_tcp_gap_count(receiver_handle recv)
{
	return get_long_stat(recv, &recv->stats.tcp_gap_count);
}

size_t receiver_get_tcp_bytes_recv(receiver_handle recv)
{
	return get_long_stat(recv, &recv->stats.tcp_bytes_recv);
}

size_t receiver_get_mcast_bytes_recv(receiver_handle recv)
{
	return get_long_stat(recv, &recv->stats.mcast_bytes_recv);
}

size_t receiver_get_mcast_packets_recv(receiver_handle recv)
{
	return get_long_stat(recv, &recv->stats.mcast_packets_recv);
}

double receiver_get_mcast_min_latency(receiver_handle recv)
{
	return get_double_stat(recv, &recv->stats.mcast_min_latency);
}

double receiver_get_mcast_max_latency(receiver_handle recv)
{
	return get_double_stat(recv, &recv->stats.mcast_max_latency);
}

double receiver_get_mcast_mean_latency(receiver_handle recv)
{
	return get_double_stat(recv, &recv->stats.mcast_mean_latency);
}

double receiver_get_mcast_stddev_latency(receiver_handle recv)
{
	double n;
	SPIN_LOCK(&recv->stats.lock);
	n = (recv->stats.mcast_packets_recv > 1 ? sqrt(recv->stats.mcast_M2_latency / (recv->stats.mcast_packets_recv - 1)) : 0);
	SPIN_UNLOCK(&recv->stats.lock);
	return n;
}
