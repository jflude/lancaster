/*
  Copyright (c)2014-2017 Peak6 Investments, LP.
  Copyright (c)2018-2024 Justin Flude.
  Use of this source code is governed by the COPYING file.
*/

#include <lancaster/error.h>
#include <lancaster/h2n2h.h>
#include <lancaster/latency.h>
#include <lancaster/poller.h>
#include <lancaster/sender.h>
#include <lancaster/sequence.h>
#include <lancaster/signals.h>
#include <lancaster/spin.h>
#include <lancaster/version.h>
#include <lancaster/xalloc.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>

#define IDLE_SLEEP_USEC 10
#define IDLE_TIMEOUT_USEC 100
#define ACCEPT_WRITE_TIMEOUT_SEC 10
#define SEND_BUFSIZ (1024 * 1024)

#if defined(DEBUG_PROTOCOL) || defined(DEBUG_GAPS)
#include <unistd.h>
#endif

#if defined(DEBUG_PROTOCOL)
#include <lancaster/dump.h>
#endif

struct sender_stats {
    long tcp_gap_count;
    long tcp_bytes_sent;
    long mcast_bytes_sent;
    long mcast_packets_sent;
};

struct sender {
    sock_handle listen_sock;
    sock_addr_handle listen_addr;
    sock_handle mcast_sock;
    sock_addr_handle sendto_addr;
    poller_handle poller;
    storage_handle store;
    identifier base_id;
    identifier max_id;
    size_t val_size;
    size_t client_count;
    revision *record_revs;
    sequence *record_seqs;
    sequence next_seq;
    sequence min_seq;
    boolean ignore_recreate;
    boolean ignore_overrun;
    microsec store_created_time;
    microsec mcast_insert_time;
    microsec mcast_send_time;
    microsec last_active_time;
    microsec heartbeat_usec;
    microsec orphan_timeout_usec;
    microsec max_pkt_age_usec;
    size_t mcast_mtu;
    char *pkt_buf;
    char *pkt_next;
    q_index last_q_idx;
    latency_handle stg_latency;
    struct sender_stats *curr_stats;
    struct sender_stats *next_stats;
    volatile spin_lock stats_lock;
    volatile boolean is_stopping;
    char hello_str[128];
#if defined(DEBUG_PROTOCOL) || defined(DEBUG_GAPS)
    FILE *debug_file;
#endif
};

#define SENDER_SEQ(sndr) (*((sequence *)sndr->pkt_buf))
#define SENDER_USEC(sndr) (*((microsec *)(sndr->pkt_buf + sizeof(sequence))))
#define SENDER_ID(sndr) (*((identifier *)sndr->pkt_next))

struct tcp_client {
    sender_handle sndr;
    sock_handle sock;
    size_t pkt_size;
    char *in_buf;
    char *in_next;
    size_t in_todo;
    char *out_buf;
    char *out_next;
    size_t out_todo;
    microsec tcp_send_time;
    struct sequence_range union_range;
    struct sequence_range reply_range;
    identifier reply_id;
    sequence min_seq_found;
#if defined(DEBUG_PROTOCOL) || defined(DEBUG_GAPS)
    char peer_name[256];
#endif
};

#define CLIENT_SEQ(clnt) (*((sequence *)clnt->out_buf))
#define CLIENT_ID(clnt) (*(identifier *)(clnt->out_buf + sizeof(sequence)))
#define CLIENT_VAL(clnt) ((void *)(clnt->out_buf + sizeof(sequence)	\
				   + sizeof(identifier)))

#if defined(DEBUG_PROTOCOL) || defined(DEBUG_GAPS)
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

static status mcast_send_pkt(sender_handle sndr)
{
    status st, st2;
    microsec now;
    sequence seq;

    if (FAILED(st = clock_time(&now)))
	return st;

    SENDER_USEC(sndr) = htonll(now);

    if (FAILED(st2 = sock_sendto(sndr->mcast_sock,
				 sndr->sendto_addr, sndr->pkt_buf,
				 sndr->pkt_next - sndr->pkt_buf)))
	return st2;

    sndr->last_active_time = sndr->mcast_send_time = now;
    seq = ntohll(SENDER_SEQ(sndr));

    if (seq >= 0 && ++sndr->next_seq == SEQUENCE_MAX)
	return error_msg(SEQUENCE_OVERFLOW,
			 "mcast_send_pkt: sequence overflow");

#if defined(DEBUG_PROTOCOL)
    fprintf(sndr->debug_file, "%s mcast send seq %07ld\n",
	    debug_time(), ntohll(SENDER_SEQ(sndr)));
#endif

    sndr->pkt_next = sndr->pkt_buf;
    sndr->mcast_insert_time = 0;

    if (FAILED(st = spin_write_lock(&sndr->stats_lock, NULL)))
	return st;

    sndr->next_stats->mcast_bytes_sent += st2;
    ++sndr->next_stats->mcast_packets_sent;

    spin_unlock(&sndr->stats_lock, 0);
    return st;
}

static status mcast_accum_record(sender_handle sndr, identifier id)
{
    status st;
    revision rev;
    microsec when;
    identifier idx = id - sndr->base_id;
    boolean sent_pkt = FALSE;
    record_handle rec = NULL;
    size_t used_sz, avail_sz;

    if (FAILED(st = storage_get_record(sndr->store, id, &rec)) ||
	FAILED(st = record_read_lock(rec, &rev)))
	return st;

    if (rev == sndr->record_revs[idx]) {
#if defined(DEBUG_PROTOCOL)
	fprintf(sndr->debug_file,
		"%s       skipping seq %07ld, id #%07ld, rev %07ld, ",
		debug_time(), sndr->next_seq, id, rev);

	fdump(record_get_value_ref(rec), NULL, 16, sndr->debug_file);
#endif
	return OK;
    }

    used_sz = sndr->pkt_next - sndr->pkt_buf;
    avail_sz = sndr->mcast_mtu - used_sz;

    if (avail_sz < (sizeof(identifier) + sndr->val_size)) {
	if (FAILED(st = mcast_send_pkt(sndr)))
	    return st;

	sent_pkt = TRUE;
	used_sz = 0;
    }

    if (used_sz == 0) {
	SENDER_SEQ(sndr) = htonll(sndr->next_seq);
	sndr->pkt_next += sizeof(sequence) + sizeof(microsec);
    }

    SENDER_ID(sndr) = htonll(id);
    sndr->pkt_next += sizeof(identifier);

    for (;;) {
	memcpy(sndr->pkt_next, record_get_value_ref(rec), sndr->val_size);
	when = record_get_timestamp(rec);

	if (rev == record_get_revision(rec))
	    break;

	if (FAILED(st = record_read_lock(rec, &rev)))
	    return st;
    }

    sndr->pkt_next += sndr->val_size;

    sndr->record_revs[idx] = rev;
    sndr->record_seqs[idx] = sndr->next_seq;

    if (FAILED(st = clock_time(&sndr->mcast_insert_time)) ||
	FAILED(st = latency_on_sample(sndr->stg_latency,
				      sndr->mcast_insert_time - when)))
	return st;

#if defined(DEBUG_PROTOCOL)
    fprintf(sndr->debug_file,
	    "%s       staging  seq %07ld, id #%07ld, rev %07ld, ",
	    debug_time(), sndr->next_seq, id, rev);

    fdump(record_get_value_ref(rec), NULL, 16, sndr->debug_file);
#endif
    return sent_pkt;
}

static status mcast_on_empty_queue(sender_handle sndr)
{
    status st;
    microsec now;

    if (!FAILED(st = clock_time(&now))) {
	if (sndr->mcast_insert_time != 0 &&
	    (now - sndr->mcast_insert_time) >= sndr->max_pkt_age_usec) {
	    st = mcast_send_pkt(sndr);
	} else {
	    if ((now - sndr->mcast_send_time) >= sndr->heartbeat_usec) {
		if (sndr->pkt_next == sndr->pkt_buf) {
		    SENDER_SEQ(sndr) = htonll(-sndr->next_seq);
		    sndr->pkt_next += sizeof(sequence) + sizeof(microsec);
#if defined(DEBUG_PROTOCOL)
		    fprintf(sndr->debug_file, "%s mcast heartbeat\n",
			    debug_time());
#endif
		}

		st = mcast_send_pkt(sndr);
	    }
	}
    }

    return st;
}

static status mcast_on_write(sender_handle sndr)
{
    status st = OK;
    q_index new_q_idx = storage_get_queue_head(sndr->store);
    q_index qi = new_q_idx - sndr->last_q_idx;

#if defined(DEBUG_PROTOCOL)
    fprintf(sndr->debug_file, "%s mcast write %ld queued\n", debug_time(), qi);
#endif

    if (qi < 0) {
	sndr->last_q_idx = new_q_idx;
	qi = 0;
    }

    if (qi == 0) {
	st = mcast_on_empty_queue(sndr);
    } else {
	size_t q_cap = storage_get_queue_capacity(sndr->store);
	if ((size_t)qi > q_cap) {
#if defined(DEBUG_PROTOCOL)
	    fprintf(sndr->debug_file, "%s mcast queue overrun\n", debug_time());
#endif
	    if (sndr->ignore_overrun)
		sndr->last_q_idx = qi - q_cap;
	    else
		return error_msg(CHANGE_QUEUE_OVERRUN,
				 "mcast_on_write: change queue overrun");
	}

	for (qi = sndr->last_q_idx; qi != new_q_idx; ++qi) {
	    identifier id;
	    if (FAILED(st = storage_read_queue(sndr->store, qi, &id)) ||
		FAILED(st = mcast_accum_record(sndr, id)) || st)
		break;
	}

	sndr->last_q_idx = qi;
	if (st == BLOCKED)
	    st = OK;
    }

    return st;
}

static status tcp_read_buf(struct tcp_client *clnt)
{
    status st = OK;
#if defined(DEBUG_PROTOCOL)
    size_t recv_sz = 0;
#endif

    while (clnt->in_todo > 0) {
	if (FAILED(st = sock_read(clnt->sock, clnt->in_next, clnt->in_todo)))
	    break;

	clnt->in_next += st;
	clnt->in_todo -= st;

#if defined(DEBUG_PROTOCOL)
	recv_sz += st;
#endif
    }

#if defined(DEBUG_PROTOCOL)
    fprintf(clnt->sndr->debug_file, "%s   %s tcp recv %lu bytes\n",
	    debug_time(), clnt->peer_name, recv_sz);
#endif
    return st;
}

static status tcp_write_buf(struct tcp_client *clnt)
{
    status st = OK;
    size_t sent_sz = 0;

    while (clnt->out_todo > 0) {
	if (FAILED(st = sock_write(clnt->sock, clnt->out_next,
				   clnt->out_todo)))
	    break;

	clnt->out_next += st;
	clnt->out_todo -= st;
	sent_sz += st;
    }

    if (sent_sz > 0) {
	status st2;
	if (FAILED(st2 = clock_time(&clnt->tcp_send_time)) ||
	    FAILED(st2 = spin_write_lock(&clnt->sndr->stats_lock, NULL)))
	    return st2;

	clnt->sndr->last_active_time = clnt->tcp_send_time;
	clnt->sndr->next_stats->tcp_bytes_sent += sent_sz;

	spin_unlock(&clnt->sndr->stats_lock, 0);
    }
#if defined(DEBUG_PROTOCOL)
    fprintf(clnt->sndr->debug_file, "%s   %s tcp sent %lu bytes\n",
	    debug_time(), clnt->peer_name, sent_sz);
#endif
    return st;
}

static status tcp_on_accept(sender_handle sndr, sock_handle sock)
{
    struct tcp_client *clnt;
    sock_handle accepted;
    status st, st2;

    char buf[512];
    strcpy(buf, sndr->hello_str);
    strcat(buf, storage_get_description(sndr->store));
    strcat(buf, "\r\n");

    if (FAILED(st = sock_accept(sock, &accepted)) ||
	FAILED(st = sock_set_tcp_nodelay(accepted, TRUE)) ||
	FAILED(st = signal_add_handler(SIGALRM)))
	return st;

    alarm(ACCEPT_WRITE_TIMEOUT_SEC);
    st = sock_write(accepted, buf, strlen(buf));

    alarm(0);
    st2 = signal_remove_handler(SIGALRM);
    if (!FAILED(st))
	st = st2;

    if (FAILED(st)) {
	if (st == (SIG_ERROR_BASE - SIGALRM))
	    st = error_msg(PROTOCOL_TIMEOUT,
			   "tcp_on_accept: protocol timed out");

	error_save_last();
	sock_destroy(&accepted);
	error_restore_last();
	return st;
    }

    clnt = XMALLOC(struct tcp_client);
    if (!clnt) {
	error_save_last();
	sock_destroy(&accepted);
	error_restore_last();
	return NO_MEMORY;
    }

    BZERO(clnt);

    clnt->in_buf = XMALLOC(struct sequence_range);
    if (!clnt->in_buf) {
	xfree(clnt);

	error_save_last();
	sock_destroy(&accepted);
	error_restore_last();
	return NO_MEMORY;
    }

    clnt->sndr = sndr;
    clnt->sock = accepted;
    clnt->in_next = clnt->in_buf;
    clnt->in_todo = sizeof(struct sequence_range);
    clnt->pkt_size = sizeof(sequence) + sizeof(identifier) + sndr->val_size;

    INVALIDATE_RANGE(clnt->union_range);

    clnt->out_buf = xmalloc(clnt->pkt_size);
    if (!clnt->out_buf) {
	xfree(clnt->in_buf);
	xfree(clnt);

	error_save_last();
	sock_destroy(&accepted);
	error_restore_last();
	return NO_MEMORY;
    }

    sock_set_property_ref(accepted, clnt);

    if (FAILED(st = sock_set_nonblock(accepted)) ||
	FAILED(st = poller_add(sndr->poller, accepted, POLLIN | POLLOUT)) ||
	FAILED(st = clock_time(&clnt->tcp_send_time)))
	return st;

    sndr->last_active_time = clnt->tcp_send_time;

    if (++sndr->client_count == 1) {
	if (FAILED(st = poller_add(sndr->poller, sndr->mcast_sock, POLLOUT)) ||
	    FAILED(st = clock_time(&sndr->mcast_send_time)))
	    return st;

	sndr->last_q_idx = storage_get_queue_head(sndr->store);
    }

#if defined(DEBUG_PROTOCOL) || defined(DEBUG_GAPS)
    if (!FAILED(st)) {
	sock_addr_handle sa;
	if (FAILED(st = sock_addr_create(&sa, NULL, 0)) ||
	    FAILED(st = sock_get_remote_address(clnt->sock, sa)) ||
	    FAILED(st = sock_addr_get_text(sa, clnt->peer_name,
					   sizeof(clnt->peer_name), TRUE)) ||
	    FAILED(st = sock_addr_destroy(&sa)))
	    return st;
    }
#endif
    return st;
}

static status close_sock_func(poller_handle poller, sock_handle sock,
			      short *events, void *param)
{
    struct tcp_client *clnt = sock_get_property_ref(sock);
    status st;
    (void)events;
    (void)param;

    if (clnt) {
	xfree(clnt->out_buf);
	xfree(clnt->in_buf);
	xfree(clnt);
    }

    if (FAILED(st = poller_remove(poller, sock)))
	return st;

    return sock_destroy(&sock);
}

static status tcp_on_hup(sender_handle sndr, sock_handle sock)
{
    status st;
    if (FAILED(st = close_sock_func(sndr->poller, sock, NULL, NULL)))
	return st;

    if (--sndr->client_count == 0)
	st = poller_remove(sndr->poller, sndr->mcast_sock);

    return st;
}

static status tcp_will_quit_func(poller_handle poller, sock_handle sock,
				 short *events, void *param)
{
    struct tcp_client *clnt = sock_get_property_ref(sock);
    status st;
    (void)poller;
    (void)events;
    (void)param;

    if (!clnt)
	return OK;

    CLIENT_SEQ(clnt) = htonll(WILL_QUIT_SEQ);
    clnt->out_next = clnt->out_buf;
    clnt->out_todo = sizeof(sequence);

    do {
	st = tcp_write_buf(clnt);
	if (st != BLOCKED && FAILED(st))
	    break;
    } while (clnt->out_todo > 0);

    return st;
}

static status tcp_write_reply(sender_handle sndr, struct tcp_client *clnt,
			      sequence seq)
{
    record_handle rec = NULL;
    revision rev;
    void *val_from;
    void *val_to = CLIENT_VAL(clnt);

    status st;
    if (FAILED(st = storage_get_record(sndr->store, clnt->reply_id, &rec)))
	return st;

    val_from = record_get_value_ref(rec);
    do {
	if (FAILED(st = record_read_lock(rec, &rev)))
	    return st;

	if (rev == 0)
	    return FALSE;

	memcpy(val_to, val_from, sndr->val_size);
    } while (rev != record_get_revision(rec));

    CLIENT_SEQ(clnt) = htonll(seq);
    CLIENT_ID(clnt) = htonll(clnt->reply_id);

    clnt->out_next = clnt->out_buf;
    clnt->out_todo = clnt->pkt_size;
    ++clnt->reply_id;

#if defined(DEBUG_PROTOCOL)
    fprintf(sndr->debug_file,
	    "%s     %s tcp gap reply seq %07ld, id #%07ld\n",
	    debug_time(), clnt->peer_name,
	    seq, clnt->reply_id - 1);
#endif
    return TRUE;
}

static status tcp_write_in_range(sender_handle sndr, struct tcp_client *clnt)
{
    for (; clnt->reply_id < sndr->max_id; ++clnt->reply_id) {
	sequence seq = sndr->record_seqs[clnt->reply_id - sndr->base_id];
	if (seq < clnt->min_seq_found)
	    clnt->min_seq_found = seq;

	if (IS_WITHIN_RANGE(clnt->reply_range, seq)) {
	    status st = tcp_write_reply(sndr, clnt, seq);
	    if (FAILED(st))
		return st;
	    else if (!st)
		break;
	}
    }

    sndr->min_seq = clnt->min_seq_found;
    INVALIDATE_RANGE(clnt->reply_range);

#if defined(DEBUG_PROTOCOL)
    fprintf(sndr->debug_file, "%s   %s tcp gap request DONE\n",
	    debug_time(), clnt->peer_name);
#endif
    return OK;
}

static status tcp_on_write(sender_handle sndr, sock_handle sock)
{
    struct tcp_client *clnt = sock_get_property_ref(sock);
    microsec now;
    status st;

#if defined(DEBUG_PROTOCOL)
    fprintf(sndr->debug_file, "%s   %s tcp write ready, %lu bytes to do\n",
	    debug_time(), clnt->peer_name, clnt->out_todo);
#endif

    st = tcp_write_buf(clnt);
    if (st == BLOCKED)
	st = OK;
    else if (FAILED(st))
	return st;

    if (clnt->out_todo != 0)
	return st;

    if (IS_VALID_RANGE(clnt->reply_range))
	return tcp_write_in_range(sndr, clnt);

    if (FAILED(st = clock_time(&now)))
	return st;

    if ((now - clnt->tcp_send_time) >= sndr->heartbeat_usec) {
	CLIENT_SEQ(clnt) = htonll(HEARTBEAT_SEQ);
	clnt->out_next = clnt->out_buf;
	clnt->out_todo = sizeof(sequence);

#if defined(DEBUG_PROTOCOL)
	fprintf(sndr->debug_file, "%s   %s tcp heartbeat\n",
		debug_time(), clnt->peer_name);
#endif
    }

    return OK;
}

static status tcp_on_read_blocked(sender_handle sndr, sock_handle sock)
{
    struct tcp_client *clnt = sock_get_property_ref(sock);

#if defined(DEBUG_PROTOCOL)
    fprintf(sndr->debug_file, "%s   %s tcp read blocked\n",
	    debug_time(), clnt->peer_name);
#endif

    if (clnt->in_next != clnt->in_buf)
	return OK;

    if (clnt->union_range.high <= sndr->min_seq)
	INVALIDATE_RANGE(clnt->union_range);
    else if (!IS_VALID_RANGE(clnt->reply_range)) {
	clnt->reply_range = clnt->union_range;
	INVALIDATE_RANGE(clnt->union_range);

	clnt->reply_id = sndr->base_id;
	clnt->min_seq_found = SEQUENCE_MAX;

#if defined(DEBUG_PROTOCOL) || defined(DEBUG_GAPS)
	fprintf(sndr->debug_file,
		"%s   %s tcp gap request seq %07ld --> %07ld\n",
		debug_time(), clnt->peer_name, clnt->reply_range.low,
		clnt->reply_range.high);
#endif
    }

    return OK;
}

static status tcp_on_read(sender_handle sndr, sock_handle sock)
{
    struct tcp_client *clnt = sock_get_property_ref(sock);
    struct sequence_range *r;
    status st;

#if defined(DEBUG_PROTOCOL)
    fprintf(sndr->debug_file, "%s   %s tcp read ready, %lu bytes to do\n",
	    debug_time(), clnt->peer_name, clnt->in_todo);
#endif

    st = tcp_read_buf(clnt);
    if (st == BLOCKED)
	st = OK;
    else if (FAILED(st))
	return st;

    if (clnt->in_todo != 0)
	return st;

    r = (struct sequence_range *)clnt->in_buf;
    r->low = ntohll(r->low);
    r->high = ntohll(r->high);

    if (!IS_VALID_RANGE(*r))
	return error_msg(PROTOCOL_ERROR,
			 "tcp_on_read: invalid sequence range");

    if (r->low < clnt->union_range.low)
	clnt->union_range.low = r->low;

    if (r->high > clnt->union_range.high)
	clnt->union_range.high = r->high;

    clnt->in_next = clnt->in_buf;
    clnt->in_todo = sizeof(struct sequence_range);

    if (FAILED(st = spin_write_lock(&sndr->stats_lock, NULL)))
	return st;

    ++sndr->next_stats->tcp_gap_count;
    spin_unlock(&sndr->stats_lock, 0);
    return st;
}

static status event_func(poller_handle poller, sock_handle sock,
			 short *revents, void *param)
{
    sender_handle sndr = param;
    status st = OK;
    (void)poller;

    if (sock == sndr->listen_sock)
	return tcp_on_accept(sndr, sock);

    if (sock == sndr->mcast_sock)
	return mcast_on_write(sndr);

    if (*revents & POLLHUP)
	return tcp_on_hup(sndr, sock);

    st = (*revents & POLLIN
	  ? tcp_on_read(sndr, sock) : tcp_on_read_blocked(sndr, sock));

    if (!FAILED(st) && (*revents & POLLOUT))
	st = tcp_on_write(sndr, sock);

    return FAILED(st) ? tcp_on_hup(sndr, sock) : st;
}

static status init(sender_handle *psndr, const char *mmap_file,
		   const char *tcp_address, unsigned short tcp_port,
		   const char *mcast_address, unsigned short mcast_port,
		   const char *mcast_interface, short mcast_ttl,
		   boolean mcast_loopback, boolean ignore_recreate,
		   boolean ignore_overrun, microsec heartbeat_usec,
		   microsec orphan_timeout_usec, microsec max_pkt_age_usec)
{
    status st;
    identifier rec_dim;
    size_t rec_rev_sz;
#if defined(DEBUG_PROTOCOL) || defined(DEBUG_GAPS)
    char debug_name[256];
#endif

    BZERO(*psndr);

    if (FAILED(st = storage_open(&(*psndr)->store, mmap_file, O_RDONLY)) ||
	FAILED(st = latency_create(&(*psndr)->stg_latency)))
	return st;

    (*psndr)->curr_stats = XMALLOC(struct sender_stats);
    if (!(*psndr)->curr_stats)
	return NO_MEMORY;

    (*psndr)->next_stats = XMALLOC(struct sender_stats);
    if (!(*psndr)->next_stats)
	return NO_MEMORY;

    BZERO((*psndr)->curr_stats);
    BZERO((*psndr)->next_stats);
    spin_create(&(*psndr)->stats_lock);

    (*psndr)->base_id = storage_get_base_id((*psndr)->store);
    (*psndr)->max_id = storage_get_max_id((*psndr)->store);
    (*psndr)->val_size = storage_get_value_size((*psndr)->store);
    (*psndr)->next_seq = 1;
    (*psndr)->min_seq = 0;
    (*psndr)->ignore_recreate = ignore_recreate;
    (*psndr)->ignore_overrun = ignore_overrun;
    (*psndr)->max_pkt_age_usec = max_pkt_age_usec;
    (*psndr)->heartbeat_usec = heartbeat_usec;
    (*psndr)->orphan_timeout_usec = orphan_timeout_usec;

    if (!ignore_recreate &&
	FAILED(st = storage_get_created_time((*psndr)->store,
					     &(*psndr)->store_created_time)))
	return st;

    rec_dim = (*psndr)->max_id - (*psndr)->base_id;
    rec_rev_sz = rec_dim * sizeof(revision);

    (*psndr)->record_revs = xmalloc(rec_rev_sz);
    if (!(*psndr)->record_revs)
	return NO_MEMORY;

    memset((*psndr)->record_revs, -1, rec_rev_sz);

    (*psndr)->record_seqs = xcalloc(rec_dim, sizeof(sequence));
    if (!(*psndr)->record_seqs)
	return NO_MEMORY;

    if (FAILED(st = sock_create(&(*psndr)->listen_sock, SOCK_STREAM, 0)) ||
	FAILED(st = sock_set_reuseaddr((*psndr)->listen_sock, TRUE)) ||
	FAILED(st = sock_addr_create(&(*psndr)->listen_addr,
				     tcp_address, tcp_port)) ||
	FAILED(st = sock_bind((*psndr)->listen_sock, (*psndr)->listen_addr)) ||
	FAILED(st = sock_listen((*psndr)->listen_sock, 5)) ||
	FAILED(st = sock_get_local_address((*psndr)->listen_sock,
					   (*psndr)->listen_addr)))
	return st;

    if (mcast_port == 0)
	mcast_port = sock_addr_get_port((*psndr)->listen_addr);

    if (FAILED(st = sock_create(&(*psndr)->mcast_sock, SOCK_DGRAM, 0)) ||
	FAILED(st = sock_set_nonblock((*psndr)->mcast_sock)))
	return st;

    if (!mcast_interface) {
	(*psndr)->mcast_mtu = DEFAULT_MTU;
    } else {
	sock_addr_handle if_addr;
	if (FAILED(st = sock_get_mtu((*psndr)->mcast_sock, mcast_interface,
				     &(*psndr)->mcast_mtu)) ||
	    FAILED(st = sock_addr_create(&if_addr, NULL, 0)) ||
	    FAILED(st = sock_get_interface_address((*psndr)->mcast_sock,
						   mcast_interface,
						   if_addr)) ||
	    FAILED(st = sock_set_mcast_interface((*psndr)->mcast_sock,
						 if_addr)) ||
	    FAILED(st = sock_addr_destroy(&if_addr)))
	    return st;
    }

    (*psndr)->mcast_mtu -= IP_OVERHEAD + UDP_OVERHEAD;
    if ((*psndr)->mcast_mtu < (sizeof(sequence) +
			       sizeof(microsec) +
			       sizeof(identifier) + (*psndr)->val_size))
	return error_msg(MTU_TOO_SMALL,
			 "sender_create: MTU too small for storage record");

    (*psndr)->pkt_buf = xmalloc((*psndr)->mcast_mtu);
    if (!(*psndr)->pkt_buf)
	return NO_MEMORY;

    (*psndr)->pkt_next = (*psndr)->pkt_buf;

    st = sprintf((*psndr)->hello_str,
		 "%d\r\n%d\r\n%s\r\n%d\r\n%lu\r\n%" PRId64 "\r\n"
		 "%" PRId64 "\r\n%lu\r\n%lu\r\n%" PRId64 "\r\n%" PRId64 "\r\n",
		 (version_get_wire_major() << 8) | version_get_wire_minor(),
		 (int)storage_get_data_version((*psndr)->store),
		 mcast_address, mcast_port, (unsigned long)(*psndr)->mcast_mtu,
		 (*psndr)->base_id, (*psndr)->max_id,
		 (unsigned long)storage_get_value_size((*psndr)->store),
		 (unsigned long)storage_get_queue_capacity((*psndr)->store),
		 max_pkt_age_usec, (*psndr)->heartbeat_usec);

    if (st < 0)
	return error_errno("sprintf");

    if (FAILED(st = sock_set_tx_buf((*psndr)->mcast_sock, SEND_BUFSIZ)) ||
	FAILED(st = sock_set_reuseaddr((*psndr)->mcast_sock, TRUE)) ||
	FAILED(st = sock_set_mcast_ttl((*psndr)->mcast_sock, mcast_ttl)) ||
	FAILED(st = sock_set_mcast_loopback((*psndr)->mcast_sock,
					    mcast_loopback)) ||
	FAILED(st = sock_addr_create(&(*psndr)->sendto_addr,
				     mcast_address, mcast_port)) ||
	FAILED(st = poller_create(&(*psndr)->poller, 10)) ||
	FAILED(st = poller_add((*psndr)->poller,
			       (*psndr)->listen_sock, POLLIN)) ||
	FAILED(st = clock_time(&(*psndr)->last_active_time)))
	return st;

#if defined(DEBUG_PROTOCOL) || defined(DEBUG_GAPS)
    sprintf(debug_name, "SEND-%s-%d-%d.DEBUG", tcp_address,
	    (int)sock_addr_get_port((*psndr)->listen_addr), (int)getpid());

    (*psndr)->debug_file = fopen(debug_name, "w");
    if (!(*psndr)->debug_file)
	st = error_errno("fopen");

    setvbuf((*psndr)->debug_file, NULL, _IOLBF, 0);
#endif
    return st;
}

status sender_create(sender_handle *psndr, const char *mmap_file,
		     const char *tcp_address, unsigned short tcp_port,
		     const char *mcast_address, unsigned short mcast_port,
		     const char *mcast_interface, short mcast_ttl,
		     boolean mcast_loopback, boolean ignore_recreate,
		     boolean ignore_overrun, microsec heartbeat_usec,
		     microsec orphan_timeout_usec, microsec max_pkt_age_usec)
{
    status st;
    if (!psndr || !mmap_file || !tcp_address || !mcast_address ||
	heartbeat_usec <= 0 || orphan_timeout_usec < 0 || max_pkt_age_usec < 0)
	return error_invalid_arg("sender_create");

    *psndr = XMALLOC(struct sender);
    if (!*psndr)
	return NO_MEMORY;

    if (FAILED(st = init(psndr, mmap_file, tcp_address, tcp_port,
			 mcast_address, mcast_port, mcast_interface,
			 mcast_ttl, mcast_loopback, ignore_recreate,
			 ignore_overrun, heartbeat_usec,
			 orphan_timeout_usec, max_pkt_age_usec))) {
	error_save_last();
	sender_destroy(psndr);
	error_restore_last();
    }

    return st;
}

status sender_destroy(sender_handle *psndr)
{
    status st = OK;
    if (!psndr || !*psndr ||
	((*psndr)->poller &&
         FAILED(st = poller_process((*psndr)->poller,
                                    close_sock_func,
                                    *psndr))) ||
	FAILED(st = poller_destroy(&(*psndr)->poller)) ||
	FAILED(st = storage_destroy(&(*psndr)->store)) ||
	FAILED(st = sock_addr_destroy(&(*psndr)->sendto_addr)) ||
	FAILED(st = sock_addr_destroy(&(*psndr)->listen_addr)) ||
	FAILED(st = latency_destroy(&(*psndr)->stg_latency)))
	return st;

    xfree((*psndr)->next_stats);
    xfree((*psndr)->curr_stats);
    xfree((*psndr)->record_seqs);
    xfree((*psndr)->record_revs);
    xfree((*psndr)->pkt_buf);

#if defined(DEBUG_PROTOCOL) || defined(DEBUG_GAPS)
    if ((*psndr)->debug_file && fclose((*psndr)->debug_file) == EOF)
	st = error_errno("fclose");
#endif

    XFREE(*psndr);
    return st;
}

storage_handle sender_get_storage(sender_handle sndr)
{
    return sndr->store;
}

unsigned short sender_get_listen_port(sender_handle sndr)
{
    return sock_addr_get_port(sndr->listen_addr);
}

status sender_run(sender_handle sndr)
{
    status st = OK, st2;

    while (!sndr->is_stopping) {
	microsec now, when;
#if defined(DEBUG_PROTOCOL)
	fprintf(sndr->debug_file, "%s ======================================\n",
		debug_time());
#endif
	if (FAILED(st = poller_events(sndr->poller, 0)) ||
	    (st > 0 &&
             FAILED(st = poller_process_events(sndr->poller, event_func,
                                               sndr))) ||
	    FAILED(st = storage_get_touched_time(sndr->store, &when)) ||
	    FAILED(st = clock_time(&now)))
	    break;

	if (sndr->orphan_timeout_usec > 0 &&
	    (now - when) >= sndr->orphan_timeout_usec) {
	    st = error_msg(STORAGE_ORPHANED, "sender_run: storage is orphaned");
	    break;
	}

	if (!sndr->ignore_recreate) {
	    if (FAILED(st = storage_get_created_time(sndr->store, &when)))
		break;

	    if (when != sndr->store_created_time) {
		st = error_msg(STORAGE_RECREATED,
			       "sender_run: storage is recreated");
		break;
	    }
	}

	if ((now - sndr->last_active_time) >= IDLE_TIMEOUT_USEC &&
	    FAILED(st = clock_sleep(IDLE_SLEEP_USEC)))
	    break;
    }

    if (sndr->client_count > 0) {
	st2 = poller_process(sndr->poller, tcp_will_quit_func, sndr);
	if (!FAILED(st))
	    st = st2;

	st2 = clock_sleep(1000000);
	if (!FAILED(st))
	    st = st2;

	st2 = poller_process(sndr->poller, close_sock_func, sndr);
	if (!FAILED(st))
	    st = st2;
    }

    return st;
}

void sender_stop(sender_handle sndr)
{
    sndr->is_stopping = TRUE;
}

long sender_get_tcp_gap_count(sender_handle sndr)
{
    return sndr->curr_stats->tcp_gap_count;
}

long sender_get_tcp_bytes_sent(sender_handle sndr)
{
    return sndr->curr_stats->tcp_bytes_sent;
}

long sender_get_mcast_bytes_sent(sender_handle sndr)
{
    return sndr->curr_stats->mcast_bytes_sent;
}

long sender_get_mcast_packets_sent(sender_handle sndr)
{
    return sndr->curr_stats->mcast_packets_sent;
}

long sender_get_receiver_count(sender_handle sndr)
{
    return sndr->client_count;
}

long sender_get_storage_record_count(sender_handle sndr)
{
    return latency_get_count(sndr->stg_latency);
}

double sender_get_storage_min_latency(sender_handle sndr)
{
    return latency_get_min(sndr->stg_latency);
}

double sender_get_storage_max_latency(sender_handle sndr)
{
    return latency_get_max(sndr->stg_latency);
}

double sender_get_storage_mean_latency(sender_handle sndr)
{
    return latency_get_mean(sndr->stg_latency);
}

double sender_get_storage_stddev_latency(sender_handle sndr)
{
    return latency_get_stddev(sndr->stg_latency);
}

status sender_roll_stats(sender_handle sndr)
{
    status st;
    struct sender_stats *tmp;
    if (FAILED(st = spin_write_lock(&sndr->stats_lock, NULL)))
	return st;

    tmp = sndr->next_stats;
    sndr->next_stats = sndr->curr_stats;
    sndr->curr_stats = tmp;

    BZERO(sndr->next_stats);
    spin_unlock(&sndr->stats_lock, 0);

    return latency_roll(sndr->stg_latency);
}
