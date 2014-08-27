#include "sender.h"
#include "accum.h"
#include "error.h"
#include "h2n2h.h"
#include "poller.h"
#include "sequence.h"
#include "spin.h"
#include "xalloc.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <sys/socket.h>

#define ORPHAN_TIMEOUT_USEC 3000000
#define IDLE_TIMEOUT_USEC 10
#define IDLE_SLEEP_USEC 1

struct sender_stats
{
	volatile int lock;
	size_t tcp_gap_count;
	size_t tcp_bytes_sent;
	size_t mcast_bytes_sent;
	size_t mcast_packets_sent;
};

struct sender
{
	sock_handle listen_sock;
	sock_handle mcast_sock;
	poller_handle poller;
	accum_handle mcast_accum;
	storage_handle store;
	queue_index last_q_idx;
	size_t mcast_mtu;
	identifier base_id;
	identifier max_id;
	size_t val_size;
	size_t client_count;
	sequence* slot_seqs;
	sequence next_seq;
	sequence min_seq;
	microsec store_created_time;
	microsec last_activity;
	microsec mcast_send_time;
	microsec heartbeat_usec;
	microsec max_pkt_age_usec;
	microsec* time_stored_at;
	struct sender_stats stats;
	volatile boolean is_stopping;
	char hello_str[128];
};

struct tcp_client
{
	sender_handle sndr;
	sock_handle sock;
	size_t pkt_size;
	char* in_buf;
	char* in_next;
	size_t in_remain;
	char* out_buf;
	char* out_next;
	size_t out_remain;
	microsec tcp_send_time;
	struct sequence_range union_range;
	struct sequence_range reply_range;
	identifier reply_id;
	sequence min_seq_found;
};

static status mcast_send_accum(sender_handle sndr)
{
	const void* data;
	size_t sz;
	microsec now;
	status st;

	if (FAILED(st = accum_get_batched(sndr->mcast_accum, &data, &sz)) || !st || FAILED(clock_time(&now)))
		return st;

	*sndr->time_stored_at = htonll(now);
	if (FAILED(st = sock_sendto(sndr->mcast_sock, data, sz)))
		return st;

	sndr->last_activity = sndr->mcast_send_time = now;

	SPIN_WRITE_LOCK(&sndr->stats.lock, no_ver);
	sndr->stats.mcast_bytes_sent += sz;
	++sndr->stats.mcast_packets_sent;
	SPIN_UNLOCK(&sndr->stats.lock, no_ver);

	accum_clear(sndr->mcast_accum);

	if (++sndr->next_seq < 0)
		error_msg("mcast_send_accum: sequence sign overflow", st = SIGN_OVERFLOW);

	return st;
}

static status mcast_accum_record(sender_handle sndr, identifier id)
{
	status st = OK;
	record_handle rec;
	identifier nid;

	if (accum_get_available(sndr->mcast_accum) < sndr->val_size + sizeof(identifier) &&
		FAILED(st = mcast_send_accum(sndr)))
		return st;
		
	if (accum_is_empty(sndr->mcast_accum)) {
		sequence seq = htonll(sndr->next_seq);
		if (FAILED(st = accum_store(sndr->mcast_accum, &seq, sizeof(seq), NULL)) ||
			FAILED(st = accum_store(sndr->mcast_accum, NULL, sizeof(microsec), (void**) &sndr->time_stored_at)))
			return st;
	}

	if (FAILED(st = storage_get_record(sndr->store, id, &rec)))
		return st;

	nid = htonll(id);
	if (!FAILED(st = accum_store(sndr->mcast_accum, &nid, sizeof(nid), NULL))) {
		void* val_at = record_get_value_ref(rec);
		void* stored_at = NULL;
		version ver;

		do {
			ver = record_read_lock(rec);
			if (stored_at)
				memcpy(stored_at, val_at, sndr->val_size);
			else if (FAILED(st = accum_store(sndr->mcast_accum, val_at, sndr->val_size, &stored_at)))
				return st;
		} while (ver != record_get_version(rec));

		sndr->slot_seqs[id - sndr->base_id] = sndr->next_seq;
	}

	return st;
}

static status mcast_on_write(sender_handle sndr)
{
	status st = OK;
	queue_index new_q_idx = storage_get_queue_head(sndr->store);
	queue_index qi = new_q_idx - sndr->last_q_idx;

	if (qi > 0) {
		if ((size_t) qi > storage_get_queue_capacity(sndr->store)) {
			error_msg("mcast_on_write: change queue overrun", st = CHANGE_QUEUE_OVERRUN);
			return st;
		}

		for (qi = sndr->last_q_idx; qi != new_q_idx; ++qi)
			if (FAILED(st = mcast_accum_record(sndr, storage_read_queue(sndr->store, qi))))
				break;

		sndr->last_q_idx = qi;
		if (st == BLOCKED)
			st = OK;
	} else if (accum_is_stale(sndr->mcast_accum))
		st = mcast_send_accum(sndr);
	else {
		microsec now;
		if (!FAILED(st = clock_time(&now)) && (now - sndr->mcast_send_time) >= sndr->heartbeat_usec) {
			if (!accum_is_empty(sndr->mcast_accum))
				st = mcast_send_accum(sndr);
			else {
				sequence hb_seq = htonll(-sndr->next_seq);
				if (!FAILED(st = accum_store(sndr->mcast_accum, &hb_seq, sizeof(hb_seq), NULL)) &&
					!FAILED(st = accum_store(sndr->mcast_accum, NULL, sizeof(microsec), (void**) &sndr->time_stored_at)))
					st = mcast_send_accum(sndr);
			}
		}
	}

	return st;
}

static status tcp_read_buf(struct tcp_client* clnt)
{
	status st = OK;
	while (clnt->in_remain > 0) {
		if (FAILED(st = sock_read(clnt->sock, clnt->in_next, clnt->in_remain)))
			break;

		clnt->in_next += st;
		clnt->in_remain -= st;
	}

	return st;
}

static status tcp_write_buf(struct tcp_client* clnt)
{
	status st = OK;
	size_t sent_sz = 0;

	while (clnt->out_remain > 0) {
		if (FAILED(st = sock_write(clnt->sock, clnt->out_next, clnt->out_remain)))
			break;

		clnt->out_next += st;
		clnt->out_remain -= st;
		sent_sz += st;
	}

	if (sent_sz > 0) {
		status st2 = clock_time(&clnt->tcp_send_time);
		if (!FAILED(st))
			st = st2;

		clnt->sndr->last_activity = clnt->tcp_send_time;

		SPIN_WRITE_LOCK(&clnt->sndr->stats.lock, no_ver);
		clnt->sndr->stats.tcp_bytes_sent += sent_sz;
		SPIN_UNLOCK(&clnt->sndr->stats.lock, no_ver);
	}

	return st;
}

static status tcp_on_accept(sender_handle sndr, sock_handle sock)
{
	struct tcp_client* clnt;
	sock_handle accepted;
	status st;

	char buf[512];
	strcpy(buf, sndr->hello_str);
	strcat(buf, storage_get_description(sndr->store));
	strcat(buf, "\r\n");

	if (FAILED(st = sock_accept(sock, &accepted)) ||
		FAILED(st = sock_write(accepted, buf, strlen(buf))))
		return st;

	clnt = XMALLOC(struct tcp_client);
	if (!clnt) {
		sock_destroy(&accepted);
		return NO_MEMORY;
	}

	BZERO(clnt);

	clnt->in_buf = XMALLOC(struct sequence_range);
	if (!clnt->in_buf) {
		xfree(clnt);
		sock_destroy(&accepted);
		return NO_MEMORY;
	}

	clnt->sndr = sndr;
	clnt->sock = accepted;
	clnt->in_next = clnt->in_buf;
	clnt->in_remain = sizeof(struct sequence_range);
	clnt->pkt_size = sizeof(sequence) + sizeof(identifier) + sndr->val_size;

	INVALIDATE_RANGE(clnt->union_range);

	clnt->out_buf = xmalloc(clnt->pkt_size);
	if (!clnt->out_buf) {
		xfree(clnt->in_buf);
		xfree(clnt);
		sock_destroy(&accepted);
		return NO_MEMORY;
	}

	sock_set_property(accepted, clnt);

	if (FAILED(st = clock_time(&clnt->tcp_send_time)) ||
		FAILED(st = sock_set_nonblock(accepted)) ||
		FAILED(st = poller_add(sndr->poller, accepted, POLLIN | POLLOUT)))
		return st;

	sndr->last_activity = clnt->tcp_send_time;

	if (++sndr->client_count == 1) {
		if (FAILED(st = poller_add(sndr->poller, sndr->mcast_sock, POLLOUT)) ||
			FAILED(st = clock_time(&sndr->mcast_send_time)))
			return st;

		sndr->last_q_idx = storage_get_queue_head(sndr->store);
	}

	return st;
}

static status close_sock_func(poller_handle poller, sock_handle sock, short* events, void* param)
{
	struct tcp_client* clnt = sock_get_property(sock);
	status st;
	(void) events; (void) param;

	if (clnt) {
		xfree(clnt->out_buf);
		xfree(clnt->in_buf);
		xfree(clnt);
	}

	if (FAILED(st = poller_remove(poller, sock)))
		return st;

	st = sock_close(sock);
	sock_destroy(&sock);
	return st;
}

static status tcp_will_quit_func(poller_handle poller, sock_handle sock, short* events, void* param)
{
	struct tcp_client* clnt = sock_get_property(sock);
	sequence* out_seq_ref;
	status st;
	(void) poller; (void) events; (void) param;

	if ( !clnt)
		return OK;

	out_seq_ref = (sequence*) clnt->out_buf;
	*out_seq_ref = htonll(WILL_QUIT_SEQ);
	clnt->out_next = clnt->out_buf;
	clnt->out_remain = sizeof(sequence);

	do {
		st = tcp_write_buf(clnt);
		if (st != BLOCKED && FAILED(st))
			break;
	} while (clnt->out_remain > 0);

	return st;
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

static status tcp_on_write(sender_handle sndr, sock_handle sock)
{
	struct tcp_client* clnt = sock_get_property(sock);
	microsec now;

	status st = tcp_write_buf(clnt);
	if (st == BLOCKED)
		st = OK;
	else if (st == EOF || st == TIMED_OUT)
		return tcp_on_hup(sndr, sock);
	else if (FAILED(st))
		return st;

	if (clnt->out_remain == 0) {
		sequence* out_seq_ref = (sequence*) clnt->out_buf;
		identifier* out_id_ref = (identifier*) (out_seq_ref + 1);

		if (IS_VALID_RANGE(clnt->reply_range)) {
			for (; clnt->reply_id < sndr->max_id; ++clnt->reply_id) {
				sequence seq = sndr->slot_seqs[clnt->reply_id - sndr->base_id];
				if (seq < clnt->min_seq_found)
					clnt->min_seq_found = seq;

				if (IS_WITHIN_RANGE(clnt->reply_range, seq)) {
					record_handle rec;
					version ver;
					void* val_at;
					if (FAILED(st = storage_get_record(sndr->store, clnt->reply_id, &rec)))
						return st;

					val_at = record_get_value_ref(rec);
					do {
						ver = record_read_lock(rec);
						memcpy(out_id_ref + 1, val_at, sndr->val_size);
					} while (ver != record_get_version(rec));

					*out_seq_ref = htonll(seq);
					*out_id_ref = htonll(clnt->reply_id);

					clnt->out_next = clnt->out_buf;
					clnt->out_remain = clnt->pkt_size;
					++clnt->reply_id;
					return OK;
				}
			}

			sndr->min_seq = clnt->min_seq_found;
			INVALIDATE_RANGE(clnt->reply_range);
		} else if (!FAILED(st = clock_time(&now)) && (now - clnt->tcp_send_time) >= sndr->heartbeat_usec) {
			*out_seq_ref = htonll(HEARTBEAT_SEQ);
			clnt->out_next = clnt->out_buf;
			clnt->out_remain = sizeof(sequence);
		}
	}

	return st;
}

static status tcp_on_read_blocked(sender_handle sndr, sock_handle sock)
{
	struct tcp_client* clnt = sock_get_property(sock);

	if (clnt->in_next == clnt->in_buf) {
		if (clnt->union_range.high <= sndr->min_seq)
			INVALIDATE_RANGE(clnt->union_range);
		else if (!IS_VALID_RANGE(clnt->reply_range)) {
			clnt->reply_id = sndr->base_id;
			clnt->reply_range = clnt->union_range;
			INVALIDATE_RANGE(clnt->union_range);
			clnt->min_seq_found = SEQUENCE_MAX;
		}
	}

	return OK;
}

static status tcp_on_read(sender_handle sndr, sock_handle sock)
{
	struct tcp_client* clnt = sock_get_property(sock);

	status st = tcp_read_buf(clnt);
	if (st == BLOCKED)
		st = OK;
	else if (st == EOF || st == TIMED_OUT)
		return tcp_on_hup(sndr, sock);
	else if (FAILED(st))
		return st;

	if (clnt->in_remain == 0) {
		struct sequence_range* r = (struct sequence_range*) clnt->in_buf;
		r->low = ntohll(r->low);
		r->high = ntohll(r->high);

		if (!IS_VALID_RANGE(*r)) {
			error_msg("tcp_on_read: invalid sequence range", st = PROTOCOL_ERROR);
			return st;
		}

		if (r->low < clnt->union_range.low)
			clnt->union_range.low = r->low;

		if (r->high > clnt->union_range.high)
			clnt->union_range.high = r->high;

		clnt->in_next = clnt->in_buf;
		clnt->in_remain = sizeof(struct sequence_range);

		SPIN_WRITE_LOCK(&sndr->stats.lock, no_ver);
		++sndr->stats.tcp_gap_count;
		SPIN_UNLOCK(&sndr->stats.lock, no_ver);
	}

	return st;
}

static status event_func(poller_handle poller, sock_handle sock, short* revents, void* param)
{
	sender_handle sndr = param;
	status st = OK;
	(void) poller;

	if (sock == sndr->listen_sock)
		return tcp_on_accept(sndr, sock);

	if (sock == sndr->mcast_sock)
		return mcast_on_write(sndr);

	if (*revents & POLLHUP)
		return tcp_on_hup(sndr, sock);

	if (FAILED(st = (*revents & POLLIN ? tcp_on_read(sndr, sock) : tcp_on_read_blocked(sndr, sock))))
		return st;

	if (*revents & POLLOUT)
		st = tcp_on_write(sndr, sock);

	return st;
}

static status get_udp_mtu(sock_handle sock, const char* dest_ip, size_t* pmtu)
{
	char device[256];
	status st;

	if (FAILED(st = sock_get_device(dest_ip, device, sizeof(device))) ||
		FAILED(st = sock_get_mtu(sock, device, pmtu)))
		return st;

	*pmtu -= IP_OVERHEAD + UDP_OVERHEAD;
	return st;
}

status sender_create(sender_handle* psndr, const char* mmap_file, microsec heartbeat_usec, long max_pkt_age_usec,
					 const char* mcast_addr, int mcast_port, int mcast_ttl, const char* tcp_addr, int tcp_port)
{
	status st;
	if (!psndr || !mmap_file || heartbeat_usec <= 0 || max_pkt_age_usec < 0 ||
		!mcast_addr || mcast_port < 0 || !tcp_addr || tcp_port < 0) {
		error_invalid_arg("sender_create");
		return FAIL;
	}

	*psndr = XMALLOC(struct sender);
	if (!*psndr)
		return NO_MEMORY;

	BZERO(*psndr);

	if (FAILED(st = storage_open(&(*psndr)->store, mmap_file, O_RDONLY))) {
		sender_destroy(psndr);
		return st;
	}

	SPIN_CREATE(&(*psndr)->stats.lock);

	(*psndr)->base_id = storage_get_base_id((*psndr)->store);
	(*psndr)->max_id = storage_get_max_id((*psndr)->store);
	(*psndr)->val_size = storage_get_value_size((*psndr)->store);
	(*psndr)->next_seq = 1;
	(*psndr)->min_seq = 0;
	(*psndr)->max_pkt_age_usec = max_pkt_age_usec;
	(*psndr)->heartbeat_usec = heartbeat_usec;
	(*psndr)->store_created_time = storage_get_created_time((*psndr)->store);

	(*psndr)->slot_seqs = xcalloc((*psndr)->max_id - (*psndr)->base_id, sizeof(sequence));
	if (!(*psndr)->slot_seqs) {
		sender_destroy(psndr);
		return NO_MEMORY;
	}

	if (FAILED(st = sock_create(&(*psndr)->listen_sock, SOCK_STREAM, tcp_addr, tcp_port)) ||
		FAILED(st = sock_set_reuseaddr((*psndr)->listen_sock, 1)) ||
		FAILED(st = sock_listen((*psndr)->listen_sock, 5)) ||
		(tcp_port == 0 && FAILED(st = sock_update_local_address((*psndr)->listen_sock)))) {
		sender_destroy(psndr);
		return st;
	}

	if (mcast_port == 0)
		mcast_port = sock_get_port((*psndr)->listen_sock);

	if (FAILED(st = sock_create(&(*psndr)->mcast_sock, SOCK_DGRAM, mcast_addr, mcast_port)) ||
		FAILED(st = sock_set_nonblock((*psndr)->mcast_sock)) ||
		FAILED(st = get_udp_mtu((*psndr)->mcast_sock, mcast_addr, &(*psndr)->mcast_mtu))) {
		sender_destroy(psndr);
		return st;
	}

	st = sprintf((*psndr)->hello_str, "%d\r\n%s\r\n%d\r\n%lu\r\n%ld\r\n%ld\r\n%lu\r\n%ld\r\n%ld\r\n",
				 STORAGE_VERSION, mcast_addr, mcast_port, (*psndr)->mcast_mtu,
				 (long) (*psndr)->base_id, (long) (*psndr)->max_id,
				 storage_get_value_size((*psndr)->store), max_pkt_age_usec, (*psndr)->heartbeat_usec);

	if (st < 0) {
		error_errno("sprintf");
		sender_destroy(psndr);
		return FAIL;
	}

	if (FAILED(st = accum_create(&(*psndr)->mcast_accum, (*psndr)->mcast_mtu, max_pkt_age_usec)) ||
		FAILED(st = sock_set_reuseaddr((*psndr)->mcast_sock, 1)) ||
		FAILED(st = sock_mcast_bind((*psndr)->mcast_sock)) ||
		FAILED(st = sock_set_mcast_ttl((*psndr)->mcast_sock, mcast_ttl)) ||
		FAILED(st = poller_create(&(*psndr)->poller, 10)) ||
		FAILED(st = poller_add((*psndr)->poller, (*psndr)->listen_sock, POLLIN))) {
		sender_destroy(psndr);
		return st;
	}

	return OK;
}

void sender_destroy(sender_handle* psndr)
{
	if (!psndr || !*psndr)
		return;

	error_save_last();

	poller_process((*psndr)->poller, close_sock_func, *psndr);

	poller_destroy(&(*psndr)->poller);
	accum_destroy(&(*psndr)->mcast_accum);
	storage_destroy(&(*psndr)->store);

	error_restore_last();

	xfree((*psndr)->slot_seqs);
	xfree(*psndr);
	*psndr = NULL;
}

storage_handle sender_get_storage(sender_handle sndr)
{
	return sndr->store;
}

int sender_get_listen_port(sender_handle sndr)
{
	return sock_get_port(sndr->listen_sock);
}

status sender_run(sender_handle sndr)
{
	status st = OK, st2;

	while (!sndr->is_stopping) {
		microsec now;
		if (FAILED(st = poller_events(sndr->poller, 0)) ||
			(st > 0 && FAILED(st = poller_process_events(sndr->poller, event_func, sndr))) ||
			FAILED(st = clock_time(&now)))
			break;

		if ((now - storage_get_touched_time(sndr->store)) >= ORPHAN_TIMEOUT_USEC) {
			error_msg("sender_run: storage is orphaned", st = STORAGE_ORPHANED);
			break;
		}

		if (storage_get_created_time(sndr->store) != sndr->store_created_time) {
			error_msg("sender_run: storage is recreated", st = STORAGE_RECREATED);
			break;
		}

		if ((now - sndr->last_activity) >= IDLE_TIMEOUT_USEC &&
			FAILED(st = clock_sleep(IDLE_SLEEP_USEC)))
			break;
	}

	st2 = poller_process(sndr->poller, tcp_will_quit_func, sndr);
	if (!FAILED(st))
		st = st2;

	st2 = clock_sleep(1000000);
	if (!FAILED(st))
		st = st2;

	st2 = poller_process(sndr->poller, close_sock_func, sndr);
	if (!FAILED(st))
		st = st2;

	return st;
}

void sender_stop(sender_handle sndr)
{
	sndr->is_stopping = TRUE;
}

static size_t get_stat(sender_handle sndr, const size_t* pval)
{
	size_t n;
	SPIN_WRITE_LOCK(&sndr->stats.lock, no_ver);
	n = *pval;
	SPIN_UNLOCK(&sndr->stats.lock, no_ver);
	return n;
}

size_t sender_get_tcp_gap_count(sender_handle sndr)
{
	return get_stat(sndr, &sndr->stats.tcp_gap_count);
}

size_t sender_get_tcp_bytes_sent(sender_handle sndr)
{
	return get_stat(sndr, &sndr->stats.tcp_bytes_sent);
}

size_t sender_get_mcast_bytes_sent(sender_handle sndr)
{
	return get_stat(sndr, &sndr->stats.mcast_bytes_sent);
}

size_t sender_get_mcast_packets_sent(sender_handle sndr)
{
	return get_stat(sndr, &sndr->stats.mcast_packets_sent);
}

size_t sender_get_receiver_count(sender_handle sndr)
{
	return sndr->client_count;
}
