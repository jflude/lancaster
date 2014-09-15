#include "receiver.h"
#include "clock.h"
#include "error.h"
#include "h2n2h.h"
#include "poller.h"
#include "sequence.h"
#include "sock.h"
#include "spin.h"
#include "xalloc.h"
#include <alloca.h>
#include <errno.h>
#include <fcntl.h>
#include <float.h>
#include <math.h>
#include <poll.h>
#include <stdio.h>
#include <time.h>

#define PROTOCOL_VERSION 1

#define RECV_BUFSIZ (64 * 1024)
#define TOUCH_PERIOD_USEC (1 * 1000000)
#define INITIAL_MC_HB_USEC (2 * 1000000)
#define IDLE_TIMEOUT_USEC 10
#define IDLE_SLEEP_USEC 1

struct receiver_stats
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

struct receiver
{
	storage_handle store;
	poller_handle poller;
	sock_handle mcast_sock;
	sock_handle tcp_sock;
	sequence* record_seqs;
	sequence next_seq;
	size_t mcast_mtu;
	identifier base_id;
	size_t val_size;
	char* in_buf;
	char* in_next;
	size_t in_remain;
	char* out_buf;
	char* out_next;
	size_t out_remain;
	microsec mcast_recv_time;
	microsec tcp_recv_time;
	microsec touched_time;
	microsec last_active_time;
	microsec timeout_usec;
	sock_addr_handle orig_src_addr;
	sock_addr_handle last_src_addr;
	volatile boolean is_stopping;
	struct receiver_stats stats;
};

static status update_stats(receiver_handle recv, size_t pkt_sz,
						   microsec now, microsec* pkt_time)
{
	microsec latency;
	double delta;

	SPIN_WRITE_LOCK(&recv->stats.lock, no_ver);

	recv->stats.mcast_bytes_recv += pkt_sz;
	++recv->stats.mcast_packets_recv;

	if (pkt_sz < (sizeof(sequence) + sizeof(microsec))) {
		SPIN_UNLOCK(&recv->stats.lock, no_ver);
		return error_msg("update_stats: packet truncated", PROTOCOL_ERROR);
	}

	latency = now - ntohll(*pkt_time);
	delta = latency - recv->stats.mcast_mean_latency;

	recv->stats.mcast_mean_latency += delta / recv->stats.mcast_packets_recv;
	recv->stats.mcast_M2_latency +=
		delta * (latency - recv->stats.mcast_mean_latency);

	if (recv->stats.mcast_min_latency == 0 ||
		latency < recv->stats.mcast_min_latency)
		recv->stats.mcast_min_latency = latency;

	if (recv->stats.mcast_max_latency == 0 ||
		latency > recv->stats.mcast_max_latency)
		recv->stats.mcast_max_latency = latency;

	SPIN_UNLOCK(&recv->stats.lock, no_ver);
	return OK;
}

static status update_record(receiver_handle recv, sequence seq,
							identifier id, void* new_val)
{
	status st;
	version ver;
	record_handle rec = NULL;

	if (FAILED(st = storage_get_record(recv->store, id, &rec)))
		return st;

	ver = record_write_lock(rec);
	memcpy(record_get_value_ref(rec), new_val, recv->val_size);
	record_set_version(rec, NEXT_VER(ver));

	recv->record_seqs[id - recv->base_id] = seq;
	return storage_write_queue(recv->store, id);
}

static void request_gap(receiver_handle recv, sequence low, sequence high)
{
	struct sequence_range* r = (struct sequence_range*) recv->out_buf;
	r->low = htonll(low);
	r->high = htonll(high);

	recv->out_next = recv->out_buf;
	recv->out_remain = sizeof(struct sequence_range);

	SPIN_WRITE_LOCK(&recv->stats.lock, no_ver);
	++recv->stats.tcp_gap_count;
	SPIN_UNLOCK(&recv->stats.lock, no_ver);
}

static status mcast_on_read(receiver_handle recv)
{
	status st, st2;
	microsec now;

	char* buf = alloca(recv->mcast_mtu);
	sequence* in_seq_ref = (sequence*) buf;
	microsec* in_stamp_ref = (microsec*) (in_seq_ref + 1);

	if (FAILED(st = st2 = sock_recvfrom(recv->mcast_sock, recv->last_src_addr,
										buf, recv->mcast_mtu)) ||
		FAILED(st = clock_time(&now)))
		return st;

	if (recv->next_seq == 1)
		sock_addr_copy(recv->orig_src_addr, recv->last_src_addr);
	else if (!sock_addr_is_equal(recv->orig_src_addr, recv->last_src_addr)) {
		char address[256];
		if (FAILED(st = sock_addr_get_text(recv->last_src_addr,
										   address, sizeof(address))))
			sprintf(address, "sock_addr_get_text failed: error #%d", (int) st);

		return error_msg("mcast_on_read: unexpected multicast source: %s",
						 UNEXPECTED_SOURCE, address);
	}

	recv->last_active_time = recv->mcast_recv_time = now;

	if (FAILED(st = update_stats(recv, st2, now, in_stamp_ref)))
		return st;

	*in_seq_ref = ntohll(*in_seq_ref);
	if (*in_seq_ref < 0)
		*in_seq_ref = -*in_seq_ref;
	else {
		char *p, *last;
		if (*in_seq_ref < recv->next_seq)
			return OK;

		p = buf + sizeof(sequence) + sizeof(microsec);
		last = buf + st2;

		for (; p < last; p += sizeof(identifier) + recv->val_size) {
			identifier* id = (identifier*) p;
			if (FAILED(st = update_record(recv, *in_seq_ref,
										  ntohll(*id), id + 1)))
				break;
		}
	}

	if (*in_seq_ref > recv->next_seq) {
		request_gap(recv, recv->next_seq, *in_seq_ref);
		recv->next_seq = *in_seq_ref;
	}

	++recv->next_seq;
	return OK;
}

static status tcp_read_buf(receiver_handle recv)
{
	status st = OK;
	size_t recv_sz = 0;

	while (recv->in_remain > 0) {
		if (FAILED(st = sock_read(recv->tcp_sock, recv->in_next,
								  recv->in_remain)))
			break;

		recv->in_next += st;
		recv->in_remain -= st;
		recv_sz += st;
	}

	if (recv_sz > 0) {
		status st2 = clock_time(&recv->tcp_recv_time);
		if (!FAILED(st))
			st = st2;

		recv->last_active_time = recv->tcp_recv_time;

		SPIN_WRITE_LOCK(&recv->stats.lock, no_ver);
		recv->stats.tcp_bytes_recv += recv_sz;
		SPIN_UNLOCK(&recv->stats.lock, no_ver);
	}

	return st;
}

static status tcp_write_buf(receiver_handle recv)
{
	status st = OK;
	while (recv->out_remain > 0) {
		if (FAILED(st = sock_write(recv->tcp_sock, recv->out_next,
								   recv->out_remain)))
			break;

		recv->out_next += st;
		recv->out_remain -= st;
	}

	return st;
}

static status tcp_on_write(receiver_handle recv)
{
	status st = tcp_write_buf(recv);
	if (st == BLOCKED)
		st = OK;

	return st;
}

static status tcp_on_read(receiver_handle recv)
{
	status st = tcp_read_buf(recv);
	if (st == BLOCKED)
		st = OK;
	else if (FAILED(st))
		return st;

	if (recv->in_remain == 0) {
		sequence* in_seq_ref = (sequence*) recv->in_buf;
		identifier* id = (identifier*) (in_seq_ref + 1);

		if ((recv->in_next - recv->in_buf) == sizeof(sequence)) {
			*in_seq_ref = ntohll(*in_seq_ref);

			if (*in_seq_ref == WILL_QUIT_SEQ) {
				recv->is_stopping = TRUE;
				return st;
			}

			if (*in_seq_ref == HEARTBEAT_SEQ) {
				recv->in_next = recv->in_buf;
				recv->in_remain = sizeof(sequence);
				return st;
			}

			recv->in_remain = sizeof(identifier) + recv->val_size;
			return OK;
		}

		*id = ntohll(*id);

		if (*in_seq_ref > recv->record_seqs[*id - recv->base_id] &&
			FAILED(st = update_record(recv, *in_seq_ref, *id, id + 1)))
			return st;

		recv->in_next = recv->in_buf;
		recv->in_remain = sizeof(sequence);
	}

	return st;
}

static status event_func(poller_handle poller, sock_handle sock,
						 short* revents, void* param)
{
	receiver_handle recv = param;
	status st = OK;
	(void) poller;

	if (sock == recv->mcast_sock)
		return mcast_on_read(recv);

	if ((*revents & POLLIN) & FAILED(st = tcp_on_read(recv)))
		return st;

	if (*revents & POLLOUT)
		st = tcp_on_write(recv);

	return st;
}

static status init(receiver_handle* precv, const char* mmap_file,
				   unsigned q_capacity, size_t property_size,
				   const char* tcp_address, unsigned short tcp_port)
{
	sock_addr_handle bind_addr = NULL, mcast_addr = NULL, iface_addr = NULL;
	char buf[512], mcast_address[32];
	int mcast_port, proto_ver, proto_len;
	long base_id, max_id, hb_usec, max_age_usec;
	size_t val_size;
	status st;

	BZERO(*precv);

	if (FAILED(st = sock_create(&(*precv)->tcp_sock,
								SOCK_STREAM, IPPROTO_TCP)) ||
		FAILED(st = sock_addr_create(&bind_addr, tcp_address, tcp_port)) ||
		FAILED(st = sock_connect((*precv)->tcp_sock, bind_addr)) ||
		FAILED(st = sock_read((*precv)->tcp_sock, buf, sizeof(buf) - 1)))

	sock_addr_destroy(&bind_addr);
	if (FAILED(st))
		return st;

	buf[st] = '\0';
	st = sscanf(buf, "%d %31s %d %lu %ld %ld %lu %ld %ld %n",
				&proto_ver, mcast_address, &mcast_port, &(*precv)->mcast_mtu,
				&base_id, &max_id, &val_size, &max_age_usec, &hb_usec,
				&proto_len);

	if (st != 9)
		return error_msg("receiver_create: invalid publisher attributes: \"%s\"",
						 PROTOCOL_ERROR, buf);

	if (proto_ver != PROTOCOL_VERSION)
		return error_msg("receiver_create: unknown protocol version: %d",
						 UNKNOWN_PROTOCOL, proto_ver);

	if (buf[proto_len] != '\0')
		buf[proto_len + strlen(buf + proto_len) - 2] = '\0';

	SPIN_CREATE(&(*precv)->stats.lock);

	(*precv)->base_id = base_id;
	(*precv)->val_size = val_size;
	(*precv)->next_seq = 1;
	(*precv)->timeout_usec = 5 * hb_usec / 2;

	(*precv)->stats.mcast_min_latency = 0;
	(*precv)->stats.mcast_max_latency = 0;

	(*precv)->in_buf = xmalloc(
		sizeof(sequence) + sizeof(identifier) + (*precv)->val_size);

	if (!(*precv)->in_buf)
		return NO_MEMORY;

	(*precv)->out_buf = XMALLOC(struct sequence_range);
	if (!(*precv)->out_buf)
		return NO_MEMORY;

	(*precv)->in_next = (*precv)->in_buf;
	(*precv)->in_remain = sizeof(sequence);

	(*precv)->record_seqs = xcalloc(max_id - base_id, sizeof(sequence));
	if (!(*precv)->record_seqs)
		return NO_MEMORY;

	if (!FAILED(st = storage_create(&(*precv)->store, mmap_file, FALSE,
									O_CREAT | O_TRUNC, base_id, max_id,
									val_size, property_size, q_capacity)) &&
		!FAILED(st = storage_set_description((*precv)->store,
											 buf + proto_len)) &&
	    !FAILED(st = sock_create(&(*precv)->mcast_sock,
								 SOCK_DGRAM, IPPROTO_UDP)) &&
		!FAILED(st = sock_set_rcvbuf((*precv)->mcast_sock, RECV_BUFSIZ)) &&
		!FAILED(st = sock_set_reuseaddr((*precv)->mcast_sock, TRUE)) &&
		!FAILED(st = sock_addr_create(&bind_addr, NULL, mcast_port)) &&
		!FAILED(st = sock_addr_create(&mcast_addr, mcast_address, mcast_port)) &&
		!FAILED(st = sock_addr_create(&iface_addr, NULL, 0)) &&
		!FAILED(st = sock_bind((*precv)->mcast_sock, bind_addr)) &&
		!FAILED(st = sock_mcast_add((*precv)->mcast_sock,
									mcast_addr, iface_addr)) &&
		!FAILED(st = sock_set_nonblock((*precv)->mcast_sock)) &&
		!FAILED(st = sock_set_nonblock((*precv)->tcp_sock)) &&
		!FAILED(st = sock_addr_create(&(*precv)->orig_src_addr, NULL, 0)) &&
		!FAILED(st = sock_addr_create(&(*precv)->last_src_addr, NULL, 0)) &&
		!FAILED(st = poller_create(&(*precv)->poller, 2)) &&
		!FAILED(st = poller_add((*precv)->poller,
								(*precv)->mcast_sock, POLLIN)) &&
		!FAILED(st = poller_add((*precv)->poller,
								(*precv)->tcp_sock, POLLIN | POLLOUT)) &&
		!FAILED(st = clock_time(&(*precv)->last_active_time)))
		(*precv)->tcp_recv_time = 
			(*precv)->mcast_recv_time = (*precv)->last_active_time;

	sock_addr_destroy(&bind_addr);
	sock_addr_destroy(&mcast_addr);
	sock_addr_destroy(&iface_addr);
	return st;
}

status receiver_create(receiver_handle* precv, const char* mmap_file,
					   unsigned q_capacity, size_t property_size,
					   const char* tcp_address, unsigned short tcp_port)
{
	status st;
	if (!precv || !mmap_file || !tcp_address)
		return error_invalid_arg("receiver_create");

	*precv = XMALLOC(struct receiver);
	if (!*precv)
		return NO_MEMORY;

	if (FAILED(st = init(precv, mmap_file, q_capacity,
						 property_size, tcp_address, tcp_port))) {
		error_save_last();
		receiver_destroy(precv);
		error_restore_last();
	}

	return st;
}

status receiver_destroy(receiver_handle* precv)
{
	status st = OK;
	if (!precv || !*precv ||
		FAILED(st = poller_destroy(&(*precv)->poller)) ||
		FAILED(st = sock_destroy(&(*precv)->mcast_sock)) ||
		FAILED(st = sock_destroy(&(*precv)->tcp_sock)) ||
		FAILED(st = sock_addr_destroy(&(*precv)->orig_src_addr)) ||
		FAILED(st = sock_addr_destroy(&(*precv)->last_src_addr)) ||
		FAILED(st = storage_destroy(&(*precv)->store)))
		return st;

	XFREE((*precv)->in_buf);
	XFREE((*precv)->out_buf);
	XFREE((*precv)->record_seqs);
	XFREE(*precv);
	return st;
}

storage_handle receiver_get_storage(receiver_handle recv)
{
	return recv->store;
}

status receiver_run(receiver_handle recv)
{
	status st = OK;

	while (!recv->is_stopping) {
		microsec now, mc_hb_usec;
		if (FAILED(st = poller_events(recv->poller, 0)) ||
			(st > 0 && FAILED(st = poller_process_events(recv->poller,
														 event_func, recv))) ||
			FAILED(st = clock_time(&now)) ||
			((now - recv->touched_time) >= TOUCH_PERIOD_USEC &&
			 FAILED(st = storage_touch(recv->store, now))))
			break;

		mc_hb_usec =
			(recv->next_seq == 1 && recv->timeout_usec < INITIAL_MC_HB_USEC
			 ? INITIAL_MC_HB_USEC : recv->timeout_usec);

		if ((now - recv->mcast_recv_time) >= mc_hb_usec) {
			st = error_msg("receiver_run: no multicast heartbeat",
						   NO_HEARTBEAT);
			break;
		}

		if ((now - recv->tcp_recv_time) >= recv->timeout_usec) {
			st = error_msg("receiver_run: no TCP heartbeat", NO_HEARTBEAT);
			break;
		}

		if ((now - recv->last_active_time) >= IDLE_TIMEOUT_USEC &&
			FAILED(st = clock_sleep(IDLE_SLEEP_USEC)))
			break;
	}

	return st;
}

void receiver_stop(receiver_handle recv)
{
	recv->is_stopping = TRUE;
}

static size_t get_long_stat(receiver_handle recv, const size_t* pval)
{
	size_t n;
	SPIN_WRITE_LOCK(&recv->stats.lock, no_ver);
	n = *pval;
	SPIN_UNLOCK(&recv->stats.lock, no_ver);
	return n;
}

static double get_double_stat(receiver_handle recv, const double* pval)
{
	double n;
	SPIN_WRITE_LOCK(&recv->stats.lock, no_ver);
	n = *pval;
	SPIN_UNLOCK(&recv->stats.lock, no_ver);
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

	n = (recv->stats.mcast_packets_recv > 1
		 ? sqrt(recv->stats.mcast_M2_latency /
				(recv->stats.mcast_packets_recv - 1))
		 : 0);

	SPIN_UNLOCK(&recv->stats.lock, no_ver);
	return n;
}
