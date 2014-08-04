#include "receiver.h"
#include "clock.h"
#include "error.h"
#include "h2n2h.h"
#include "sock.h"
#include "spin.h"
#include "thread.h"
#include "xalloc.h"
#include <alloca.h>
#include <errno.h>
#include <fcntl.h>
#include <float.h>
#include <math.h>
#include <poll.h>
#include <stdio.h>
#include <time.h>
#include <netinet/in.h>
#include <sys/socket.h>

#define HEARTBEAT_SEQ -1
#define WILL_QUIT_SEQ -2

struct receiver_stats_t
{
	volatile int lock;
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
	microsec_t last_mcast_recv;
	microsec_t last_tcp_recv;
	microsec_t heartbeat_usec;
	struct sockaddr_in mcast_addr;
	struct receiver_stats_t stats;
};

static void* mcast_quit(receiver_handle me, status st)
{
	status st2 = sock_close(me->mcast_sock);
	if (!FAILED(st))
		st = st2;

	sock_destroy(&me->mcast_sock);
	return (void*) (long) st;
}

static void* mcast_func(thread_handle thr)
{
	receiver_handle me = thread_get_param(thr);
	size_t val_size = storage_get_value_size(me->store);
	char* buf = alloca(me->mcast_mtu);
	sequence* recv_seq = (sequence*) buf;
	microsec_t* recv_stamp = (microsec_t*) (recv_seq + 1);
	status st, st2;

	if (FAILED(st = clock_time(&me->last_mcast_recv)))
		return mcast_quit(me, st);

	while (!thread_is_stopping(thr)) {
		const char *p, *last;
		microsec_t now;
		double latency, delta;

		st = sock_recvfrom(me->mcast_sock, buf, me->mcast_mtu);
		if (st == BLOCKED) {
			if (FAILED(st = clock_time(&now)))
				break;

			if ((now - me->last_mcast_recv) > me->heartbeat_usec) {
				error_heartbeat("mcast_func");
				st = HEARTBEAT;
				break;
			}

			if (FAILED(st = clock_sleep(1)))
				break;

			continue;
		} else if (FAILED(st))
			break;

		if (FAILED(st2 = clock_time(&now))) {
			st = st2;
			break;
		}

		if (me->next_seq == 1)
			me->mcast_addr = *sock_get_address(me->mcast_sock);
		else {
			const struct sockaddr_in* addr = sock_get_address(me->mcast_sock);
			if (addr->sin_addr.s_addr != me->mcast_addr.sin_addr.s_addr || addr->sin_port != me->mcast_addr.sin_port) {
				errno = EEXIST;
				error_errno("mcast_func");
				st = FAIL;
				break;
			}
		}

		if (FAILED(st2 = storage_set_send_recv_time(me->store, me->last_mcast_recv = now))) {
			st = st2;
			break;
		}

		SPIN_WRITE_LOCK(&me->stats.lock, no_ver);
		me->stats.mcast_bytes_recv += st;
		me->stats.mcast_packets_recv++;

		if ((unsigned) st < (sizeof(*recv_seq) + sizeof(*recv_stamp))) {
			SPIN_UNLOCK(&me->stats.lock, no_ver);
			errno = EPROTO;
			error_errno("mcast_func");
			st = BAD_PROTOCOL;
			break;
		}

		latency = now - ntohll(*recv_stamp);
		delta = latency - me->stats.mcast_mean_latency;
		me->stats.mcast_mean_latency += delta / me->stats.mcast_packets_recv;
		me->stats.mcast_M2_latency += delta * (latency - me->stats.mcast_mean_latency);

		if (latency < me->stats.mcast_min_latency)
			me->stats.mcast_min_latency = latency;

		if (latency > me->stats.mcast_max_latency)
			me->stats.mcast_max_latency = latency;

		SPIN_UNLOCK(&me->stats.lock, no_ver);

		*recv_seq = ntohll(*recv_seq);
		if (*recv_seq < me->next_seq)
			continue;

		last = buf + st;
		for (p = buf + sizeof(*recv_seq) + sizeof(*recv_stamp); p < last; p += val_size + sizeof(identifier)) {
			record_handle rec;
			identifier* id = (identifier*) p;

			*id = ntohll(*id);
			if (FAILED(st = storage_get_record(me->store, *id, &rec)))
				goto finish;

			record_write_lock(rec);
			memcpy(record_get_value_ref(rec), id + 1, val_size);
			record_set_sequence(rec, *recv_seq);

			if (FAILED(st = storage_write_queue(me->store, *id)))
				goto finish;
		}

		if (*recv_seq > me->next_seq) {
			struct storage_seq_range_t range;
			char* p = (void*) &range;
			size_t sz = sizeof(range);

			range.low = htonll(me->next_seq);
			me->next_seq = *recv_seq;
			range.high = htonll(me->next_seq);

			while (sz > 0) {
				if (thread_is_stopping(thr))
					goto finish;

				st = sock_write(me->tcp_sock, p, sz);
				if (st == BLOCKED) {
					if (FAILED(st = clock_sleep(1)))
						break;

					continue;
				} else if (FAILED(st))
					goto finish;

				p += st;
				sz -= st;
			}

			SPIN_WRITE_LOCK(&me->stats.lock, no_ver);
			me->stats.tcp_gap_count++;
			SPIN_UNLOCK(&me->stats.lock, no_ver);
		}

		me->next_seq++;
	}

finish:
	return mcast_quit(me, st);
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
			microsec_t now;
			if (FAILED(st = clock_time(&now)))
				break;

			if ((now - me->last_tcp_recv) > me->heartbeat_usec) {
				error_heartbeat("tcp_func");
				st = HEARTBEAT;
				break;
			}

			if (FAILED(st = clock_sleep(1)))
				break;

			continue;
		} else if (FAILED(st))
			break;

		buf += st;
		sz -= st;
		bytes_in += st;

		if (FAILED(st = clock_time(&me->last_tcp_recv)))
			break;

		st = TRUE;
	}

	if (bytes_in > 0) {
		status st2;
		SPIN_WRITE_LOCK(&me->stats.lock, no_ver);
		me->stats.tcp_bytes_recv += bytes_in;
		SPIN_UNLOCK(&me->stats.lock, no_ver);

		if (FAILED(st2 = storage_set_send_recv_time(me->store, me->last_tcp_recv)))
			return st2;
	}

	return st;
}

static void* tcp_quit(receiver_handle me, status st)
{
	status st2 = sock_close(me->tcp_sock);
	if (!FAILED(st))
		st = st2;

	sock_destroy(&me->tcp_sock);
	return (void*) (long) st;
}

static void* tcp_func(thread_handle thr)
{
	receiver_handle me = thread_get_param(thr);
	size_t val_size = storage_get_value_size(me->store);
	size_t pkt_size = sizeof(sequence) + sizeof(identifier) + val_size;
	char* buf = alloca(pkt_size);

	sequence* recv_seq = (sequence*) buf;
	identifier* id = (identifier*) (recv_seq + 1);
	status st;

	if (FAILED(st = clock_time(&me->last_tcp_recv)))
		return tcp_quit(me, st);

	while (!thread_is_stopping(thr)) {
		record_handle rec;
		sequence seq;
		st = tcp_read(me, buf, sizeof(*recv_seq));
		if (FAILED(st) || !st)
			break;

		*recv_seq = ntohll(*recv_seq);
		if (*recv_seq == HEARTBEAT_SEQ)
			continue;

		if (*recv_seq == WILL_QUIT_SEQ)
			break;

		st = tcp_read(me, buf + sizeof(*recv_seq), pkt_size - sizeof(*recv_seq));
		if (FAILED(st) || !st)
			break;

		*id = ntohll(*id);
		if (FAILED(st = storage_get_record(me->store, *id, &rec)))
			break;

		seq = record_write_lock(rec);
		if (*recv_seq <= seq) {
			record_set_sequence(rec, seq);
			continue;
		}

		memcpy(record_get_value_ref(rec), id + 1, val_size);
		record_set_sequence(rec, *recv_seq);

		if (FAILED(st = storage_write_queue(me->store, *id)))
			break;
	}

	return tcp_quit(me, st);
}

status receiver_create(receiver_handle* precv, const char* mmap_file, unsigned q_capacity, const char* tcp_addr, int tcp_port)
{
	char buf[128], mcast_addr[32];
	int proto_ver, mcast_port;
	long base_id, max_id, hb_usec, max_age_usec;
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
	st = sscanf(buf, "%d %31s %d %lu %ld %ld %lu %ld %ld",
				&proto_ver, mcast_addr, &mcast_port, &(*precv)->mcast_mtu, &base_id, &max_id, &val_size, &max_age_usec, &hb_usec);

	if (st == EOF) {
		errno = EPROTO;
		error_errno("receiver_create");
		return BAD_PROTOCOL;
	}	

	if (st != 9 || proto_ver != STORAGE_VERSION) {
		errno = EPROTONOSUPPORT;
		error_errno("receiver_create");
		return BAD_PROTOCOL;
	}

	SPIN_CREATE(&(*precv)->stats.lock);

	(*precv)->next_seq = 1;
	(*precv)->heartbeat_usec = 5 * hb_usec / 2;
	(*precv)->stats.mcast_min_latency = DBL_MAX;
	(*precv)->stats.mcast_max_latency = DBL_MIN;

	if (FAILED(st = storage_create(&(*precv)->store, mmap_file, O_CREAT | O_TRUNC, q_capacity, base_id, max_id, val_size)) ||
	    FAILED(st = sock_create(&(*precv)->mcast_sock, SOCK_DGRAM, mcast_addr, mcast_port)) ||
		FAILED(st = sock_reuseaddr((*precv)->mcast_sock, 1)) ||
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
	SPIN_WRITE_LOCK(&me->stats.lock, no_ver);
	n = *pval;
	SPIN_UNLOCK(&me->stats.lock, no_ver);
	return n;
}

static size_t get_double_stat(receiver_handle me, const double* pval)
{
	double n;
	SPIN_WRITE_LOCK(&me->stats.lock, no_ver);
	n = *pval;
	SPIN_UNLOCK(&me->stats.lock, no_ver);
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
	SPIN_WRITE_LOCK(&recv->stats.lock, no_ver);
	n = (recv->stats.mcast_packets_recv > 1 ? sqrt(recv->stats.mcast_M2_latency / (recv->stats.mcast_packets_recv - 1)) : 0);
	SPIN_UNLOCK(&recv->stats.lock, no_ver);
	return n;
}
