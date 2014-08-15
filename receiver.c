#include "receiver.h"
#include "clock.h"
#include "error.h"
#include "h2n2h.h"
#include "sequence.h"
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

#define RECV_BUFSIZ (64 * 1024)
#define TOUCH_USEC (1000 * 1000)
#define MIN_HB_PAD_USEC (2 * 1000 * 1000)

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
	sequence* seq_array;
	sequence next_seq;
	size_t mcast_mtu;
	microsec_t last_mcast_recv;
	microsec_t last_tcp_recv;
	microsec_t last_touch;
	microsec_t timeout_usec;
	microsec_t touch_usec;
	long orig_mcast_addr;
	int orig_mcast_port;
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
	identifier base_id = storage_get_base_id(me->store);
	char* buf = alloca(me->mcast_mtu);
	sequence* recv_seq = (sequence*) buf;
	microsec_t* recv_stamp = (microsec_t*) (recv_seq + 1);
	status st, st2;

	if (FAILED(st = clock_time(&me->last_mcast_recv)))
		return mcast_quit(me, st);

	if (me->timeout_usec < MIN_HB_PAD_USEC)
		me->last_mcast_recv += MIN_HB_PAD_USEC;

	while (!thread_is_stopping(thr)) {
		const char *p, *last;
		microsec_t now, latency;
		double delta;

		st = sock_recvfrom(me->mcast_sock, buf, me->mcast_mtu);
		if (st == BLOCKED) {
			if (FAILED(st = clock_time(&now)))
				break;

			if ((now - me->last_mcast_recv) > me->timeout_usec) {
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

		if (me->next_seq == 1) {
			me->orig_mcast_addr = sock_get_address(me->mcast_sock);
			me->orig_mcast_port = sock_get_port(me->mcast_sock);
		} else if (sock_get_address(me->mcast_sock) != me->orig_mcast_addr ||
				   sock_get_port(me->mcast_sock) != me->orig_mcast_port) {
			errno = EEXIST;
			error_errno("mcast_func");
			st = FAIL;
			break;
		}

		me->last_mcast_recv = now;

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
		if (*recv_seq < 0)
			*recv_seq = -*recv_seq;
		else {
			if (*recv_seq < me->next_seq)
				continue;

			last = buf + st;
			for (p = buf + sizeof(*recv_seq) + sizeof(*recv_stamp); p < last; p += val_size + sizeof(identifier)) {
				version ver;
				record_handle rec;
				identifier* id = (identifier*) p;

				*id = ntohll(*id);
				if (FAILED(st = storage_get_record(me->store, *id, &rec)))
					goto finish;

				ver = record_write_lock(rec);
				memcpy(record_get_value_ref(rec), id + 1, val_size);
				me->seq_array[*id - base_id] = *recv_seq;
				record_set_version(rec, NEXT_VER(ver));

				if (FAILED(st = storage_write_queue(me->store, *id)))
					goto finish;
			}
		}

		if (*recv_seq > me->next_seq) {
			struct sequence_range_t range;
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

			if ((now - me->last_tcp_recv) > me->timeout_usec) {
				error_heartbeat("tcp_read");
				st = HEARTBEAT;
				break;
			}

			if ((now - me->last_touch) > me->touch_usec) {
				if (FAILED(st = storage_touch(me->store)))
					break;

				me->last_touch = now;
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
		SPIN_WRITE_LOCK(&me->stats.lock, no_ver);
		me->stats.tcp_bytes_recv += bytes_in;
		SPIN_UNLOCK(&me->stats.lock, no_ver);
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
	identifier base_id = storage_get_base_id(me->store);
	char* buf = alloca(pkt_size);

	sequence* recv_seq = (sequence*) buf;
	identifier* id = (identifier*) (recv_seq + 1);
	status st;

	if (FAILED(st = clock_time(&me->last_tcp_recv)))
		return tcp_quit(me, st);

	if (me->timeout_usec < MIN_HB_PAD_USEC)
		me->last_tcp_recv += MIN_HB_PAD_USEC;

	while (!thread_is_stopping(thr)) {
		record_handle rec;
		version ver;
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

		ver = record_write_lock(rec);
		if (*recv_seq <= me->seq_array[*id - base_id]) {
			record_set_version(rec, ver);
			continue;
		}

		memcpy(record_get_value_ref(rec), id + 1, val_size);
		me->seq_array[*id - base_id] = *recv_seq;
		record_set_version(rec, NEXT_VER(ver));

		if (FAILED(st = storage_write_queue(me->store, *id)))
			break;
	}

	return tcp_quit(me, st);
}

status receiver_create(receiver_handle* precv, const char* mmap_file, unsigned q_capacity, const char* tcp_addr, int tcp_port)
{
	char buf[512], mcast_addr[32];
	int mcast_port, proto_ver, proto_len;
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
	st = sscanf(buf, "%d %31s %d %lu %ld %ld %lu %ld %ld %n",
				&proto_ver, mcast_addr, &mcast_port, &(*precv)->mcast_mtu,
				&base_id, &max_id, &val_size, &max_age_usec, &hb_usec, &proto_len);

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

	if (buf[proto_len] != '\0')
		buf[proto_len + strlen(buf + proto_len) - 2] = '\0';

	SPIN_CREATE(&(*precv)->stats.lock);

	(*precv)->next_seq = 1;
	(*precv)->touch_usec = TOUCH_USEC;
	(*precv)->timeout_usec = 5 * hb_usec / 2;
	(*precv)->stats.mcast_min_latency = DBL_MAX;
	(*precv)->stats.mcast_max_latency = DBL_MIN;

	(*precv)->seq_array = xcalloc(max_id - base_id, sizeof(sequence));
	if (!(*precv)->seq_array) {
		receiver_destroy(precv);
		return NO_MEMORY;
	}

	if (FAILED(st = storage_create(&(*precv)->store, mmap_file, O_CREAT | O_TRUNC, base_id, max_id, val_size, q_capacity)) ||
		FAILED(st = storage_set_description((*precv)->store, buf + proto_len)) ||
	    FAILED(st = sock_create(&(*precv)->mcast_sock, SOCK_DGRAM, mcast_addr, mcast_port)) ||
		FAILED(st = sock_set_rcvbuf((*precv)->mcast_sock, RECV_BUFSIZ)) ||
		FAILED(st = sock_set_reuseaddr((*precv)->mcast_sock, 1)) ||
		FAILED(st = sock_mcast_bind((*precv)->mcast_sock)) ||
		FAILED(st = sock_set_nonblock((*precv)->mcast_sock)) ||
		FAILED(st = sock_set_nonblock((*precv)->tcp_sock)) ||
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

	xfree((*precv)->seq_array);
	xfree(*precv);
	*precv = NULL;
}

storage_handle receiver_get_storage(receiver_handle recv)
{
	return recv->store;
}

boolean receiver_is_running(receiver_handle recv)
{
	return recv->tcp_thr && recv->mcast_thr && thread_is_running(recv->tcp_thr) && thread_is_running(recv->mcast_thr);
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
