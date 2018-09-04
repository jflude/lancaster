/*
  Copyright (c)2014-2018 Peak6 Investments, LP.  All rights reserved.
  Use of this source code is governed by the COPYING file.
*/

#include <lancaster/clock.h>
#include <lancaster/error.h>
#include <lancaster/h2n2h.h>
#include <lancaster/latency.h>
#include <lancaster/poller.h>
#include <lancaster/receiver.h>
#include <lancaster/sequence.h>
#include <lancaster/signals.h>
#include <lancaster/socket.h>
#include <lancaster/spin.h>
#include <lancaster/version.h>
#include <lancaster/xalloc.h>
#include <errno.h>
#include <fcntl.h>
#include <float.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "xalloca.h"

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#define INITIAL_MC_HB_USEC (10 * 1000000)
#define CONNECT_READ_TIMEOUT_SEC 10
#define RECV_BUFSIZ (1024 * 1024)

#if defined(DEBUG_PROTOCOL)
#include <unistd.h>
#include <lancaster/dump.h>
#endif

struct receiver_stats {
    long tcp_gap_count;
    long tcp_bytes_recv;
    long mcast_bytes_recv;
};

struct receiver {
    storage_handle store;
    poller_handle poller;
    sock_handle mcast_sock;
    sock_handle tcp_sock;
    sock_addr_handle tcp_addr;
    sequence *record_seqs;
    sequence next_seq;
    size_t mcast_mtu;
    identifier base_id;
    size_t val_size;
    char *in_buf;
    char *in_next;
    size_t in_todo;
    char *out_buf;
    char *out_next;
    size_t out_todo;
    microsec mcast_recv_time;
    microsec tcp_recv_time;
    microsec touched_time;
    microsec touch_period_usec;
    microsec timeout_usec;
    sock_addr_handle mcast_src_addr;
    sock_addr_handle mcast_pub_addr;
    latency_handle mcast_latency;
    struct receiver_stats *curr_stats;
    struct receiver_stats *next_stats;
    volatile spin_lock stats_lock;
    volatile boolean is_stopping;
#if defined(DEBUG_PROTOCOL)
    FILE *debug_file;
#endif
};

#if defined(DEBUG_PROTOCOL)
static const char *debug_time(void)
{
    microsec now;
    static char buf[64];

    if (FAILED(clock_time(&now)) ||
	FAILED(clock_get_short_text(now, 6, buf, sizeof(buf))))
	error_report_fatal();

    return buf;
}
#endif

static status update_stats(receiver_handle recv, size_t pkt_sz, microsec delay)
{
    status st;
    if (FAILED(st = spin_write_lock(&recv->stats_lock, NULL)))
	return st;

    recv->next_stats->mcast_bytes_recv += pkt_sz;
    spin_unlock(&recv->stats_lock, 0);

    return latency_on_sample(recv->mcast_latency, delay);
}

static status update_record(receiver_handle recv, sequence seq, identifier id,
			    void *new_val, microsec when)
{
    status st;
    revision rev;
    record_handle rec = NULL;

    if (FAILED(st = storage_get_record(recv->store, id, &rec)) ||
	FAILED(st = record_write_lock(rec, &rev)))
	return st;

    memcpy(record_get_value_ref(rec), new_val, recv->val_size);

    record_set_timestamp(rec, when);
    record_set_revision(rec, NEXT_REV(rev));

    recv->record_seqs[id - recv->base_id] = seq;
    if (storage_get_queue_capacity(recv->store) > 0 &&
	FAILED(st = storage_write_queue(recv->store, id)))
	return st;

#if defined(DEBUG_PROTOCOL)
    fprintf(recv->debug_file,
	    "%s       updating seq %07ld, id #%07ld, rev %07ld, ",
	    debug_time(), seq, id, NEXT_REV(rev));

    fdump(record_get_value_ref(rec), NULL, 16, recv->debug_file);
#endif
    return st;
}

static status request_gap(receiver_handle recv, sequence low, sequence high)
{
    struct sequence_range *r = (struct sequence_range *)recv->out_buf;
    status st;

    r->low = htonll(low);
    r->high = htonll(high);

    recv->out_next = recv->out_buf;
    recv->out_todo = sizeof(struct sequence_range);

    if (FAILED(st = poller_set_event(recv->poller, recv->tcp_sock,
				     POLLIN | POLLOUT)) ||
	FAILED(st = spin_write_lock(&recv->stats_lock, NULL)))
	return st;

    ++recv->next_stats->tcp_gap_count;
    spin_unlock(&recv->stats_lock, 0);

#if defined(DEBUG_PROTOCOL)
    fprintf(recv->debug_file, "%s   tcp gap request seq %07ld --> %07ld\n",
	    debug_time(), low, high);
#endif
    return st;
}

static status get_sock_addr_text(sock_addr_handle addr, char *text,
				 size_t text_sz, boolean with_port)
{
    status st;
    if (FAILED(st = sock_addr_get_text(addr, text, text_sz, with_port)))
	sprintf(text, "sock_addr_get_text failed: error #%d", (int)st);

    return st;
}

static status mcast_on_read(receiver_handle recv)
{
    status st, st2;
    microsec now;
    boolean is_hb;
    unsigned long mcast_ip, tcp_ip;

    char *buf = alloca(recv->mcast_mtu);
    sequence *in_seq_ref = (sequence *)buf;
    microsec *in_stamp_ref = (microsec *)(in_seq_ref + 1);

    if (FAILED(st = st2 = sock_recvfrom(recv->mcast_sock, recv->mcast_src_addr,
					buf, recv->mcast_mtu)) ||
	FAILED(st = clock_time(&now)))
	return st;

    if ((size_t)st2 < (sizeof(sequence) + sizeof(microsec)))
	return error_msg(PROTOCOL_ERROR, "mcast_on_read: packet truncated");

    mcast_ip = sock_addr_get_ip(recv->mcast_src_addr);
    tcp_ip = sock_addr_get_ip(recv->tcp_addr);

    if (mcast_ip != tcp_ip) {
	char pub[256], src[256], tcp[256];
	get_sock_addr_text(recv->mcast_pub_addr, pub, sizeof(pub), TRUE);
	get_sock_addr_text(recv->mcast_src_addr, src, sizeof(src), FALSE);
	get_sock_addr_text(recv->tcp_addr, tcp, sizeof(tcp), TRUE);
	return error_msg(UNEXPECTED_SOURCE,
			 "mcast_on_read: unexpected source: "
			 "%s from %s, not %s", pub, src, tcp);
    }

    recv->mcast_recv_time = now;

    if (FAILED(st = update_stats(recv, st2, now - ntohll(*in_stamp_ref))))
	return st;

    *in_seq_ref = ntohll(*in_seq_ref);

#if defined(DEBUG_PROTOCOL)
    if (*in_seq_ref < 0) {
	fprintf(recv->debug_file, "%s mcast heartbeat seq %07ld\n",
		debug_time(), -*in_seq_ref);
    } else {
	fprintf(recv->debug_file, "%s mcast recv seq %07ld\n",
		debug_time(), *in_seq_ref);
    }
#endif

    if (*in_seq_ref < 0) {
	*in_seq_ref = -*in_seq_ref;
	is_hb = TRUE;
    } else {
	char *p, *last;
	if (*in_seq_ref < recv->next_seq)
	    return OK;

	p = buf + sizeof(sequence) + sizeof(microsec);
	last = buf + st2;

	for (; p < last; p += sizeof(identifier) + recv->val_size) {
	    identifier *id = (identifier *)p;
	    if (FAILED(st = update_record(recv, *in_seq_ref,
					  ntohll(*id), id + 1, now)))
		return st;
	}

	is_hb = FALSE;
    }

    if (*in_seq_ref > recv->next_seq) {
	if (FAILED(st = request_gap(recv, recv->next_seq, *in_seq_ref)))
	    return st;

	recv->next_seq = *in_seq_ref;
    }

    if (!is_hb)
	++recv->next_seq;

    return OK;
}

static status tcp_read_buf(receiver_handle recv)
{
    status st = OK;
    size_t recv_sz = 0;

    while (recv->in_todo > 0) {
	if (FAILED(st = sock_read(recv->tcp_sock, recv->in_next,
				  recv->in_todo)))
	    break;

	recv->in_next += st;
	recv->in_todo -= st;
	recv_sz += st;
    }

    if (recv_sz > 0) {
	status st2;
	if (FAILED(st2 = clock_time(&recv->tcp_recv_time)) ||
	    FAILED(st2 = spin_write_lock(&recv->stats_lock, NULL)))
	    return st2;

	recv->next_stats->tcp_bytes_recv += recv_sz;
	spin_unlock(&recv->stats_lock, 0);
    }
#if defined(DEBUG_PROTOCOL)
    fprintf(recv->debug_file, "%s   tcp recv %lu bytes\n",
	    debug_time(), recv_sz);
#endif
    return st;
}

static status tcp_write_buf(receiver_handle recv)
{
    status st = OK;
#if defined(DEBUG_PROTOCOL)
    size_t sent_sz = 0;
#endif

    while (recv->out_todo > 0) {
	if (FAILED(st = sock_write(recv->tcp_sock, recv->out_next,
				   recv->out_todo)))
	    break;

	recv->out_next += st;
	recv->out_todo -= st;

#if defined(DEBUG_PROTOCOL)
	sent_sz += st;
#endif
    }

#if defined(DEBUG_PROTOCOL)
    fprintf(recv->debug_file, "%s   tcp sent %lu bytes\n",
	    debug_time(), sent_sz);
#endif
    return st;
}

static status tcp_on_write(receiver_handle recv)
{
    status st = tcp_write_buf(recv);
    if (st == BLOCKED)
	st = OK;

    if (recv->out_todo == 0) {
	status st2 = poller_set_event(recv->poller, recv->tcp_sock, POLLIN);
	if (!FAILED(st))
	    st = st2;
    }

    return st;
}

static status tcp_on_read(receiver_handle recv)
{
    status st = tcp_read_buf(recv);
    if (st == BLOCKED)
	st = OK;
    else if (FAILED(st))
	return st;

    if (recv->in_todo == 0) {
	sequence *in_seq_ref = (sequence *)recv->in_buf;
	identifier *id = (identifier *)(in_seq_ref + 1);

	if ((recv->in_next - recv->in_buf) == sizeof(sequence)) {
	    *in_seq_ref = ntohll(*in_seq_ref);

	    if (*in_seq_ref == WILL_QUIT_SEQ) {
#if defined(DEBUG_PROTOCOL)
		fprintf(recv->debug_file, "%s   tcp will quit\n",
			debug_time());
#endif
		recv->is_stopping = TRUE;
		return st;
	    }

	    if (*in_seq_ref == HEARTBEAT_SEQ) {
#if defined(DEBUG_PROTOCOL)
		fprintf(recv->debug_file, "%s   tcp heartbeat\n",
			debug_time());
#endif
		recv->in_next = recv->in_buf;
		recv->in_todo = sizeof(sequence);
		return st;
	    }

	    recv->in_todo = sizeof(identifier) + recv->val_size;
	    return OK;
	}

	*id = ntohll(*id);

#if defined(DEBUG_PROTOCOL)
	fprintf(recv->debug_file,
		"%s     tcp gap reply seq %07ld, id #%07ld\n",
		debug_time(), *in_seq_ref, *id);
#endif
	if (*in_seq_ref > recv->record_seqs[*id - recv->base_id]) {
	    microsec now;
	    if (FAILED(st = clock_time(&now)) ||
		FAILED(st = update_record(recv, *in_seq_ref,
					  *id, id + 1, now)))
		return st;
	}

	recv->in_next = recv->in_buf;
	recv->in_todo = sizeof(sequence);
    }

    return st;
}

static status event_func(poller_handle poller, sock_handle sock,
			 short *revents, void *param)
{
    receiver_handle recv = param;
    status st = OK;
    (void)poller;

    if (sock == recv->mcast_sock)
	return mcast_on_read(recv);

    if ((*revents & POLLIN) & FAILED(st = tcp_on_read(recv)))
	return st;

    if (*revents & POLLOUT)
	st = tcp_on_write(recv);

    return st;
}

static status init(receiver_handle *precv, const char *mmap_file,
		   mode_t mode_flags, size_t property_size,
		   size_t q_capacity, microsec touch_period_usec,
		   unsigned max_missed_hb, const char *tcp_address,
		   unsigned short tcp_port)
{
    sock_addr_handle bind_addr = NULL, iface_addr = NULL;
    char buf[512], mcast_address[32];
    unsigned wire_ver, data_ver;
    int wire_ver_len, mcast_port, proto_len;
    long base_id, max_id, hb_usec, max_age_usec;
    size_t pub_q_capacity, val_size, rec_seq_sz;
    status st, st2;
#if defined(DEBUG_PROTOCOL)
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
    spin_create(&(*precv)->stats_lock);

    if (FAILED(st = latency_create(&(*precv)->mcast_latency)) ||
	FAILED(st = sock_create(&(*precv)->tcp_sock,
				SOCK_STREAM, IPPROTO_TCP)) ||
	FAILED(st = sock_addr_create(&(*precv)->tcp_addr,
				     tcp_address, tcp_port)) ||
	FAILED(st = sock_connect((*precv)->tcp_sock, (*precv)->tcp_addr)) ||
	FAILED(st = signal_add_handler(SIGALRM)))
	return st;

    alarm(CONNECT_READ_TIMEOUT_SEC);
    st = sock_read((*precv)->tcp_sock, buf, sizeof(buf) - 1);

    alarm(0);
    st2 = signal_remove_handler(SIGALRM);
    if (!FAILED(st))
	st = st2;

    if (FAILED(st)) {
	if (st == (SIG_ERROR_BASE - SIGALRM))
	    st = error_msg(PROTOCOL_TIMEOUT,
			   "receiver_create: protocol timed out");

	return st;
    }

    buf[st] = '\0';
    st = sscanf(buf, "%u %n", &wire_ver, &wire_ver_len);
    if (st != 1)
	return error_msg(PROTOCOL_ERROR,
			 "receiver_create: invalid wire version:\n%s", buf);

    if ((wire_ver >> 8) != version_get_wire_major())
	return error_msg(WRONG_WIRE_VERSION,
			 "receiver_create: incompatible wire version "
			 "(%d.%d but expecting %d.%d)",
			 wire_ver >> 8, wire_ver & 0xFF,
			 version_get_wire_major(),
			 version_get_wire_minor());

    st = sscanf(buf + wire_ver_len,
		"%u %31s %d %lu %ld %ld %lu %lu %ld %ld %n",
		&data_ver, mcast_address, &mcast_port,
		&(*precv)->mcast_mtu, &base_id, &max_id, &val_size,
		&pub_q_capacity, &max_age_usec, &hb_usec, &proto_len);

    if (st != 10)
	return error_msg(PROTOCOL_ERROR,
			 "receiver_create: invalid publisher attributes:\n%s",
			 buf);

    proto_len += wire_ver_len;
    if (buf[proto_len] != '\0')
	buf[proto_len + strlen(buf + proto_len) - 2] = '\0';

    (*precv)->base_id = base_id;
    (*precv)->val_size = val_size;
    (*precv)->next_seq = 0;
    (*precv)->touched_time = 0;
    (*precv)->touch_period_usec = touch_period_usec;
    (*precv)->timeout_usec = hb_usec * (max_missed_hb + 1) + 100000;

    if (q_capacity == SENDER_QUEUE_CAPACITY)
	q_capacity = pub_q_capacity;

    (*precv)->in_buf =
	xmalloc(sizeof(sequence) + sizeof(identifier) + (*precv)->val_size);

    if (!(*precv)->in_buf)
	return NO_MEMORY;

    (*precv)->out_buf = XMALLOC(struct sequence_range);
    if (!(*precv)->out_buf)
	return NO_MEMORY;

    (*precv)->in_next = (*precv)->in_buf;
    (*precv)->in_todo = sizeof(sequence);

    rec_seq_sz = (max_id - base_id) * sizeof(sequence);
    (*precv)->record_seqs = xmalloc(rec_seq_sz);
    if (!(*precv)->record_seqs)
	return NO_MEMORY;

    memset((*precv)->record_seqs, -1, rec_seq_sz);

    if (!FAILED(st = storage_create(&(*precv)->store, mmap_file,
				    O_RDWR | O_CREAT, mode_flags, TRUE,
				    base_id, max_id, val_size, property_size,
				    q_capacity, buf + proto_len)) &&
	!FAILED(st = storage_set_data_version((*precv)->store, data_ver)) &&
	!FAILED(st = sock_create(&(*precv)->mcast_sock,
				 SOCK_DGRAM, IPPROTO_UDP)) &&
	!FAILED(st = sock_set_rx_buf((*precv)->mcast_sock, RECV_BUFSIZ)) &&
	!FAILED(st = sock_set_reuseaddr((*precv)->mcast_sock, TRUE)) &&
	!FAILED(st = sock_addr_create(&(*precv)->mcast_pub_addr, mcast_address,
				      mcast_port)) &&
	!FAILED(st = sock_addr_create(&iface_addr, NULL, 0)) &&
#ifdef CYGWIN_OS
	!FAILED(st = sock_addr_create(&bind_addr, NULL, mcast_port)) &&
#else
	!FAILED(st = sock_addr_create(&bind_addr, mcast_address,
				      mcast_port)) &&
#endif
	!FAILED(st = sock_bind((*precv)->mcast_sock, bind_addr)) &&
	!FAILED(st = sock_mcast_add((*precv)->mcast_sock,
				    (*precv)->mcast_pub_addr, iface_addr)) &&
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

#if defined(DEBUG_PROTOCOL)
	sprintf(debug_name, "RECV-%s-%d-%d.DEBUG",
		tcp_address, (int)tcp_port, (int)getpid());

	(*precv)->debug_file = fopen(debug_name, "w");
	if (!(*precv)->debug_file)
	    st = error_errno("fopen");

	setvbuf((*precv)->debug_file, NULL, _IOLBF, 0);
#endif
    }

    st2 = sock_addr_destroy(&iface_addr);
    if (!FAILED(st))
	st = st2;

    return st;
}

status receiver_create(receiver_handle *precv, const char *mmap_file,
		       mode_t mode_flags, size_t property_size,
		       size_t q_capacity, microsec touch_period_usec,
		       unsigned max_missed_hb, const char *tcp_address,
		       unsigned short tcp_port)
{
    status st;
    if (!precv || !mmap_file || touch_period_usec < 0 || !tcp_address)
	return error_invalid_arg("receiver_create");

    *precv = XMALLOC(struct receiver);
    if (!*precv)
	return NO_MEMORY;

    if (FAILED(st = init(precv, mmap_file, mode_flags, property_size,
			 q_capacity, touch_period_usec, max_missed_hb,
			 tcp_address, tcp_port))) {
	error_save_last();
	receiver_destroy(precv);
	error_restore_last();
    }

    return st;
}

status receiver_destroy(receiver_handle *precv)
{
    status st = OK;
    if (!precv || !*precv ||
	FAILED(st = poller_destroy(&(*precv)->poller)) ||
	FAILED(st = sock_destroy(&(*precv)->mcast_sock)) ||
	FAILED(st = sock_destroy(&(*precv)->tcp_sock)) ||
	FAILED(st = sock_addr_destroy(&(*precv)->tcp_addr)) ||
	FAILED(st = sock_addr_destroy(&(*precv)->mcast_src_addr)) ||
	FAILED(st = storage_destroy(&(*precv)->store)) ||
	FAILED(st = latency_destroy(&(*precv)->mcast_latency)))
	return st;

    xfree((*precv)->next_stats);
    xfree((*precv)->curr_stats);
    xfree((*precv)->record_seqs);
    xfree((*precv)->out_buf);
    xfree((*precv)->in_buf);

#if defined(DEBUG_PROTOCOL)
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
#if defined(DEBUG_PROTOCOL)
	fprintf(recv->debug_file,
		"%s ======================================\n",
		debug_time());
#endif
	if (FAILED(st = poller_events(recv->poller, 10)) ||
	    (st > 0 && FAILED(st = poller_process_events(recv->poller,
							 event_func, recv))) ||
	    FAILED(st = clock_time(&now)) ||
	    (recv->touch_period_usec > 0 &&
	     (now - recv->touched_time) >= recv->touch_period_usec &&
	     FAILED(st = storage_touch(recv->store,
				       recv->touched_time = now))))
	    break;

	mc_hb_usec =
	    (recv->next_seq == 0 && recv->timeout_usec < INITIAL_MC_HB_USEC
	     ? INITIAL_MC_HB_USEC : recv->timeout_usec);

	if ((now - recv->mcast_recv_time) >= mc_hb_usec) {
#if defined(DEBUG_PROTOCOL)
	    fprintf(recv->debug_file, "%s mcast no heartbeat\n", debug_time());
#endif
	    st = error_msg(NO_HEARTBEAT,
			   "receiver_run: no multicast heartbeat");
	    break;
	}

	if ((now - recv->tcp_recv_time) >= recv->timeout_usec) {
#if defined(DEBUG_PROTOCOL)
	    fprintf(recv->debug_file, "%s   tcp no heartbeat\n", debug_time());
#endif
	    st = error_msg(NO_HEARTBEAT, "receiver_run: no TCP heartbeat");
	    break;
	}
    }

    return st;
}

void receiver_stop(receiver_handle recv)
{
    recv->is_stopping = TRUE;
}

long receiver_get_tcp_gap_count(receiver_handle recv)
{
    return recv->curr_stats->tcp_gap_count;
}

long receiver_get_tcp_bytes_recv(receiver_handle recv)
{
    return recv->curr_stats->tcp_bytes_recv;
}

long receiver_get_mcast_bytes_recv(receiver_handle recv)
{
    return recv->curr_stats->mcast_bytes_recv;
}

long receiver_get_mcast_packets_recv(receiver_handle recv)
{
    return latency_get_count(recv->mcast_latency);
}

double receiver_get_mcast_min_latency(receiver_handle recv)
{
    return latency_get_min(recv->mcast_latency);
}

double receiver_get_mcast_max_latency(receiver_handle recv)
{
    return latency_get_max(recv->mcast_latency);
}

double receiver_get_mcast_mean_latency(receiver_handle recv)
{
    return latency_get_mean(recv->mcast_latency);
}

double receiver_get_mcast_stddev_latency(receiver_handle recv)
{
    return latency_get_stddev(recv->mcast_latency);
}

status receiver_roll_stats(receiver_handle recv)
{
    status st;
    struct receiver_stats *tmp;
    if (FAILED(st = spin_write_lock(&recv->stats_lock, NULL)))
	return st;

    tmp = recv->next_stats;
    recv->next_stats = recv->curr_stats;
    recv->curr_stats = tmp;

    BZERO(recv->next_stats);
    spin_unlock(&recv->stats_lock, 0);

    return latency_roll(recv->mcast_latency);
}
