#include "receiver.h"
#include "clock.h"

#include "dump.h"

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

#define RECV_BUFSIZ (1024 * 1024)
#define TOUCH_PERIOD_USEC (1 * 1000000)
#define INITIAL_MC_HB_USEC (2 * 1000000)

#ifdef DEBUG_PROTOCOL
#include "dump.h"
#include <sys/types.h>
#include <unistd.h>
#endif

struct receiver_stats
{
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
	sock_addr_handle tcp_addr;
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
	microsec timeout_usec;
	sock_addr_handle mcast_src_addr;
	sock_addr_handle mcast_addr;
	struct receiver_stats* curr_stats;
	struct receiver_stats* next_stats;
	volatile int stats_lock;
	volatile boolean is_stopping;
#ifdef DEBUG_PROTOCOL
	FILE* debug_file;
#endif
};

static status update_stats(receiver_handle recv, size_t pkt_sz,
						   microsec now, microsec* pkt_time)
{
	double latency, delta;
	SPIN_WRITE_LOCK(&recv->stats_lock, no_rev);

	recv->next_stats->mcast_bytes_recv += pkt_sz;

	latency = now - ntohll(*pkt_time);
	delta = latency - recv->next_stats->mcast_mean_latency;

	recv->next_stats->mcast_mean_latency +=
		delta / ++recv->next_stats->mcast_packets_recv;

	recv->next_stats->mcast_M2_latency +=
		delta * (latency - recv->next_stats->mcast_mean_latency);

	if (recv->next_stats->mcast_min_latency == 0 ||
		latency < recv->next_stats->mcast_min_latency)
		recv->next_stats->mcast_min_latency = latency;

	if (recv->next_stats->mcast_max_latency == 0 ||
		latency > recv->next_stats->mcast_max_latency)
		recv->next_stats->mcast_max_latency = latency;

	SPIN_UNLOCK(&recv->stats_lock, no_rev);
	return OK;
}

static status update_record(receiver_handle recv, sequence seq,
							identifier id, void* new_val)
{
	status st;
	revision rev;
	record_handle rec = NULL;

	if (FAILED(st = storage_get_record(recv->store, id, &rec)))
		return st;

	rev = record_write_lock(rec);
	memcpy(record_get_value_ref(rec), new_val, recv->val_size);
	record_set_revision(rec, NEXT_REV(rev));

	recv->record_seqs[id - recv->base_id] = seq;
	if (FAILED(st = storage_write_queue(recv->store, id)))
		return st;

#ifdef DEBUG_PROTOCOL
	if (fprintf(recv->debug_file,
				"\t\t\tupdating seq %07ld, id #%07ld, rev %07ld, ",
				seq, id, NEXT_REV(rev)) < 0)
		return (feof(recv->debug_file) ? error_eof : error_errno)("fprintf");

	st = fdump(record_get_value_ref(rec), 16, FALSE, recv->debug_file);
#endif
	return st;
}

static status request_gap(receiver_handle recv, sequence low, sequence high)
{
	struct sequence_range* r = (struct sequence_range*) recv->out_buf;
	status st;

	r->low = htonll(low);
	r->high = htonll(high);

	recv->out_next = recv->out_buf;
	recv->out_remain = sizeof(struct sequence_range);

	if (FAILED(st = poller_set_event(recv->poller, recv->tcp_sock,
									 POLLIN | POLLOUT)))
		return st;

	SPIN_WRITE_LOCK(&recv->stats_lock, no_rev);
	++recv->next_stats->tcp_gap_count;
	SPIN_UNLOCK(&recv->stats_lock, no_rev);

#ifdef DEBUG_PROTOCOL
	if (fprintf(recv->debug_file, "\tgap request seq %07ld --> %07ld\n",
				low, high) < 0)
		st = (feof(recv->debug_file) ? error_eof : error_errno)("fprintf");
#endif
	return st;
}

void long_to_ip(unsigned long addr, char* buff){
	sprintf(buff,"%lu.%lu.%lu.%lu",(addr>>24) & 0xff, (addr>>16)& 0xff, (addr>>8)& 0xff, addr & 0xFF);
}
static status mcast_on_read(receiver_handle recv)
{
	status st, st2;
	microsec now;
	unsigned long mcast_ip, tcp_ip;

	char* buf = alloca(recv->mcast_mtu);
	sequence* in_seq_ref = (sequence*) buf;
	microsec* in_stamp_ref = (microsec*) (in_seq_ref + 1);

	if (FAILED(st = st2 = sock_recvfrom(recv->mcast_sock, recv->mcast_src_addr,
										buf, recv->mcast_mtu)) ||
		FAILED(st = clock_time(&now)))
		return st;

	if ((size_t) st2 < (sizeof(sequence) + sizeof(microsec)))
		return error_msg("mcast_on_read: packet truncated", PROTOCOL_ERROR);

	mcast_ip = sock_addr_get_ip(recv->mcast_src_addr);
	tcp_ip = sock_addr_get_ip(recv->tcp_addr);
	if (mcast_ip != tcp_ip) {
		char src_text[256];
		char tcp_text[256];
		char mcast_text[256];
		if (FAILED(st = sock_addr_get_text(recv->mcast_src_addr,
										   src_text, sizeof(src_text))))
			sprintf(src_text, "sock_addr_get_text failed: error #%d", (int) st);
		if (FAILED(st = sock_addr_get_text(recv->tcp_addr,
										   tcp_text, sizeof(tcp_text))))
			sprintf(tcp_text, "sock_addr_get_text failed: error #%d", (int) st);
		if (FAILED(st = sock_addr_get_text(recv->mcast_addr,
										   mcast_text, sizeof(mcast_text))))
			sprintf(tcp_text, "sock_addr_get_text failed: error #%d", (int) st);

		return error_msg("mcast_on_read of: %s, expected source: %s, got: %s",
						 UNEXPECTED_SOURCE, mcast_text, tcp_text, src_text);
	}

	recv->mcast_recv_time = now;

	if (FAILED(st = update_stats(recv, st2, now, in_stamp_ref)))
		return st;

	*in_seq_ref = ntohll(*in_seq_ref);

#ifdef DEBUG_PROTOCOL
	if (fprintf(recv->debug_file, "mcast recv seq %07ld\n", *in_seq_ref) < 0)
		return (feof(recv->debug_file) ? error_eof : error_errno)("fprintf");
#endif

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
		if (FAILED(st = request_gap(recv, recv->next_seq, *in_seq_ref)))
			return st;

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

		SPIN_WRITE_LOCK(&recv->stats_lock, no_rev);
		recv->next_stats->tcp_bytes_recv += recv_sz;
		SPIN_UNLOCK(&recv->stats_lock, no_rev);
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

	if (recv->out_remain == 0)
		st = poller_set_event(recv->poller, recv->tcp_sock, POLLIN);

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
#ifdef DEBUG_PROTOCOL
				if (fprintf(recv->debug_file, "WILL QUIT\n") < 0)
					st = (feof(recv->debug_file)
						  ? error_eof : error_errno)("fprintf");
#endif
				recv->is_stopping = TRUE;
				return st;
			}

			if (*in_seq_ref == HEARTBEAT_SEQ) {
#ifdef DEBUG_PROTOCOL
				if (fprintf(recv->debug_file, "TCP heartbeat\n") < 0)
					st = (feof(recv->debug_file)
						  ? error_eof : error_errno)("fprintf");
#endif
				recv->in_next = recv->in_buf;
				recv->in_remain = sizeof(sequence);
				return st;
			}

			recv->in_remain = sizeof(identifier) + recv->val_size;
			return OK;
		}

		*id = ntohll(*id);

#ifdef DEBUG_PROTOCOL
		if (fprintf(recv->debug_file, "\t\tgap response seq %07ld, id #%07ld\n",
					*in_seq_ref, *id) < 0)
			return (feof(recv->debug_file)
					? error_eof : error_errno)("fprintf");
#endif
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
	char buf[512], wire_ver[8], mcast_address[32];
	static char expected_ver[] = WIRE_VERSION;
	int mcast_port, proto_len;
	long base_id, max_id, hb_usec, max_age_usec;
	size_t val_size;
	status st;
#ifdef DEBUG_PROTOCOL
	char debug_name[256];
#endif

	BZERO(*precv);

	(*precv)->curr_stats = XMALLOC(struct receiver_stats);
	if (!(*precv)->curr_stats)
		return NO_MEMORY;

	(*precv)->next_stats = XMALLOC(struct receiver_stats);
	if (!(*precv)->next_stats)
		return NO_MEMORY;

	BZERO((*precv)->curr_stats);
	BZERO((*precv)->next_stats);
	SPIN_CREATE(&(*precv)->stats_lock);

	if (FAILED(st = sock_create(&(*precv)->tcp_sock,
								SOCK_STREAM, IPPROTO_TCP)) ||
		FAILED(st = sock_addr_create(&(*precv)->tcp_addr, tcp_address, tcp_port)) ||
		FAILED(st = sock_connect((*precv)->tcp_sock, (*precv)->tcp_addr)) ||
		FAILED(st = sock_read((*precv)->tcp_sock, buf, sizeof(buf) - 1)))
		return st;

	buf[st] = '\0';
	st = sscanf(buf, "%7s %31s %d %lu %ld %ld %lu %ld %ld %n",
				wire_ver, mcast_address, &mcast_port, &(*precv)->mcast_mtu,
				&base_id, &max_id, &val_size, &max_age_usec, &hb_usec,
				&proto_len);

	if (st != 9)
		return error_msg("receiver_create: invalid publisher attributes: \"%s\"",
						 PROTOCOL_ERROR, buf);

	st = strchr(expected_ver, '.') - expected_ver;

	if (strlen(wire_ver) < (size_t) st ||
		strncmp(expected_ver, wire_ver, st) != 0)
		return error_msg("receiver_create: wrong wire version: %s (expecting "
						 WIRE_VERSION ")", WIRE_WRONG_VERSION, wire_ver);

	if (buf[proto_len] != '\0')
		buf[proto_len + strlen(buf + proto_len) - 2] = '\0';

	(*precv)->base_id = base_id;
	(*precv)->val_size = val_size;
	(*precv)->next_seq = 0;
	(*precv)->timeout_usec = 5 * hb_usec / 2;

	(*precv)->in_buf =
		xmalloc(sizeof(sequence) + sizeof(identifier) + (*precv)->val_size);

	if (!(*precv)->in_buf)
		return NO_MEMORY;

	(*precv)->out_buf = XMALLOC(struct sequence_range);
	if (!(*precv)->out_buf)
		return NO_MEMORY;

	(*precv)->in_next = (*precv)->in_buf;
	(*precv)->in_remain = sizeof(sequence);

	(*precv)->record_seqs = xmalloc((max_id - base_id) * sizeof(sequence));
	if (!(*precv)->record_seqs)
		return NO_MEMORY;

	memset((*precv)->record_seqs, -1, (max_id - base_id) * sizeof(sequence));

	if (!FAILED(st = storage_create(&(*precv)->store, mmap_file, TRUE,
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
		!FAILED(st = sock_addr_create(&(*precv)->mcast_src_addr, NULL, 0)) &&
		!FAILED(st = poller_create(&(*precv)->poller, 2)) &&
		!FAILED(st = poller_add((*precv)->poller,
								(*precv)->mcast_sock, POLLIN)) &&
		!FAILED(st = poller_add((*precv)->poller,
								(*precv)->tcp_sock, POLLIN)) &&
		!FAILED(st = clock_time(&(*precv)->mcast_recv_time))) {
		(*precv)->tcp_recv_time = (*precv)->mcast_recv_time;
		sock_addr_create(&(*precv)->mcast_addr, mcast_address, mcast_port);

#ifdef DEBUG_PROTOCOL
		sprintf(debug_name, "RECV-%s-%d-%d.DEBUG",
				tcp_address, (int) tcp_port, (int) getpid());

		(*precv)->debug_file = fopen(debug_name, "w");
		if (!(*precv)->debug_file)
			st = error_errno("fopen");
#endif
	}

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
		FAILED(st = sock_addr_destroy(&(*precv)->tcp_addr)) ||
		FAILED(st = sock_addr_destroy(&(*precv)->mcast_src_addr)) ||
		FAILED(st = storage_destroy(&(*precv)->store)))
		return st;

	XFREE((*precv)->in_buf);
	XFREE((*precv)->out_buf);
	XFREE((*precv)->record_seqs);
	XFREE((*precv)->next_stats);
	XFREE((*precv)->curr_stats);

#ifdef DEBUG_PROTOCOL
	if ((*precv)->debug_file && fclose((*precv)->debug_file) == EOF)
		st = error_errno("fclose");
#endif

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
		if (FAILED(st = poller_events(recv->poller, 10)) ||
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
	}

	return st;
}

void receiver_stop(receiver_handle recv)
{
	recv->is_stopping = TRUE;
}

size_t receiver_get_tcp_gap_count(receiver_handle recv)
{
	return recv->curr_stats->tcp_gap_count;
}

size_t receiver_get_tcp_bytes_recv(receiver_handle recv)
{
	return recv->curr_stats->tcp_bytes_recv;
}

size_t receiver_get_mcast_bytes_recv(receiver_handle recv)
{
	return recv->curr_stats->mcast_bytes_recv;
}

size_t receiver_get_mcast_packets_recv(receiver_handle recv)
{
	return recv->curr_stats->mcast_packets_recv;
}

double receiver_get_mcast_min_latency(receiver_handle recv)
{
	return recv->curr_stats->mcast_min_latency;
}

double receiver_get_mcast_max_latency(receiver_handle recv)
{
	return recv->curr_stats->mcast_max_latency;
}

double receiver_get_mcast_mean_latency(receiver_handle recv)
{
	return recv->curr_stats->mcast_mean_latency;
}

double receiver_get_mcast_stddev_latency(receiver_handle recv)
{
	return (recv->curr_stats->mcast_packets_recv > 1
			? sqrt(recv->curr_stats->mcast_M2_latency /
				   (recv->curr_stats->mcast_packets_recv - 1))
			: 0);
}

void receiver_next_stats(receiver_handle recv)
{
	struct receiver_stats* tmp;
	SPIN_WRITE_LOCK(&recv->stats_lock, no_rev);

	tmp = recv->next_stats;
	recv->next_stats = recv->curr_stats;
	recv->curr_stats = tmp;

	BZERO(recv->next_stats);
	SPIN_UNLOCK(&recv->stats_lock, no_rev);
}
