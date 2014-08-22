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
	microsec last_activity;
	microsec last_mcast_send;
	microsec heartbeat_usec;
	microsec max_pkt_age_usec;
	microsec* time_stored_at;
	struct sender_stats stats;
	boolean is_stopping;
	char hello_str[128];
};

struct tcp_client
{
	sender_handle sender;
	sock_handle sock;
	size_t pkt_size;
	char* in_buf;
	char* in_next;
	size_t in_remain;
	struct sequence_range* in_range_ref;
	char* out_buf;
	char* out_next;
	size_t out_remain;
	sequence* out_seq_ref;
	identifier* out_id_ref;
	microsec last_tcp_send;
	struct sequence_range union_range;
	struct sequence_range reply_range;
	identifier reply_id;
	sequence min_seq_found;
};

static status mcast_send_accum(sender_handle sender)
{
	const void* data;
	size_t sz;
	microsec now;
	status st;

	if (FAILED(st = accum_get_batched(sender->mcast_accum, &data, &sz)) || !st || FAILED(clock_time(&now)))
		return st;

	*sender->time_stored_at = htonll(now);
	if (FAILED(st = sock_sendto(sender->mcast_sock, data, sz)))
		return st;

	sender->last_activity = sender->last_mcast_send = now;

	SPIN_WRITE_LOCK(&sender->stats.lock, no_ver);
	sender->stats.mcast_bytes_sent += sz;
	++sender->stats.mcast_packets_sent;
	SPIN_UNLOCK(&sender->stats.lock, no_ver);

	accum_clear(sender->mcast_accum);

	if (++sender->next_seq < 0) {
		errno = EOVERFLOW;
		error_errno("mcast_send_accum");
		return FAIL;
	}

	return st;
}

static status mcast_accum_record(sender_handle sender, identifier id)
{
	status st = OK;
	record_handle rec;
	identifier nid;

	if (accum_get_available(sender->mcast_accum) < sender->val_size + sizeof(identifier) &&
		FAILED(st = mcast_send_accum(sender)))
		return st;
		
	if (accum_is_empty(sender->mcast_accum)) {
		sequence seq = htonll(sender->next_seq);
		if (FAILED(st = accum_store(sender->mcast_accum, &seq, sizeof(seq), NULL)) ||
			FAILED(st = accum_store(sender->mcast_accum, NULL, sizeof(microsec), (void**) &sender->time_stored_at)))
			return st;
	}

	if (FAILED(st = storage_get_record(sender->store, id, &rec)))
		return st;

	nid = htonll(id);
	if (!FAILED(st = accum_store(sender->mcast_accum, &nid, sizeof(nid), NULL))) {
		void* val_at = record_get_value_ref(rec);
		void* stored_at = NULL;
		version ver;

		do {
			ver = record_read_lock(rec);
			if (stored_at)
				memcpy(stored_at, val_at, sender->val_size);
			else if (FAILED(st = accum_store(sender->mcast_accum, val_at, sender->val_size, &stored_at)))
				return st;
		} while (ver != record_get_version(rec));

		sender->slot_seqs[id - sender->base_id] = sender->next_seq;
	}

	return st;
}

static status mcast_on_write(sender_handle sender)
{
	status st = OK;
	queue_index new_q_idx = storage_get_queue_head(sender->store);
	queue_index qi = new_q_idx - sender->last_q_idx;

	if (qi > 0) {
		if ((size_t) qi > storage_get_queue_capacity(sender->store)) {
			errno = ERANGE;
			error_errno("mcast_on_write");
			return FAIL;
		}

		for (qi = sender->last_q_idx; qi != new_q_idx; ++qi)
			if (FAILED(st = mcast_accum_record(sender, storage_read_queue(sender->store, qi))))
				break;

		sender->last_q_idx = qi;
		if (st == BLOCKED)
			st = OK;
	} else if (accum_is_stale(sender->mcast_accum))
		st = mcast_send_accum(sender);
	else {
		microsec now;
		if (!FAILED(st = clock_time(&now)) && (now - sender->last_mcast_send) >= sender->heartbeat_usec) {
			if (!accum_is_empty(sender->mcast_accum))
				st = mcast_send_accum(sender);
			else {
				sequence hb_seq = htonll(-sender->next_seq);
				if (!FAILED(st = accum_store(sender->mcast_accum, &hb_seq, sizeof(hb_seq), NULL)) &&
					!FAILED(st = accum_store(sender->mcast_accum, NULL, sizeof(microsec), (void**) &sender->time_stored_at)))
					st = mcast_send_accum(sender);
			}
		}
	}

	return st;
}

static status tcp_read_buf(struct tcp_client* client)
{
	status st = OK;
	while (client->in_remain > 0) {
		if (FAILED(st = sock_read(client->sock, client->in_next, client->in_remain)))
			break;

		client->in_next += st;
		client->in_remain -= st;
	}

	return st;
}

static status tcp_write_buf(struct tcp_client* client)
{
	status st = OK;
	size_t sent_sz = 0;

	while (client->out_remain > 0) {
		if (FAILED(st = sock_write(client->sock, client->out_next, client->out_remain)))
			break;

		client->out_next += st;
		client->out_remain -= st;
		sent_sz += st;
	}

	if (sent_sz > 0) {
		status st2 = clock_time(&client->last_tcp_send);
		if (!FAILED(st))
			st = st2;

		client->sender->last_activity = client->last_tcp_send;

		SPIN_WRITE_LOCK(&client->sender->stats.lock, no_ver);
		client->sender->stats.tcp_bytes_sent += sent_sz;
		SPIN_UNLOCK(&client->sender->stats.lock, no_ver);
	}

	return st;
}

static status tcp_on_accept(sender_handle sender, sock_handle sock)
{
	struct tcp_client* client;
	sock_handle accepted;
	status st;

	char buf[512];
	strcpy(buf, sender->hello_str);
	strcat(buf, storage_get_description(sender->store));
	strcat(buf, "\r\n");

	if (FAILED(st = sock_accept(sock, &accepted)) ||
		FAILED(st = sock_write(accepted, buf, strlen(buf))))
		return st;

	client = XMALLOC(struct tcp_client);
	if (!client) {
		sock_destroy(&accepted);
		return NO_MEMORY;
	}

	BZERO(client);

	client->in_buf = xmalloc(sizeof(*client->in_range_ref));
	if (!client->in_buf) {
		xfree(client);
		sock_destroy(&accepted);
		return NO_MEMORY;
	}

	client->pkt_size = sizeof(sequence) + sizeof(identifier) + sender->val_size;

	client->out_buf = xmalloc(client->pkt_size);
	if (!client->out_buf) {
		xfree(client->in_buf);
		xfree(client);
		sock_destroy(&accepted);
		return NO_MEMORY;
	}

	client->sender = sender;
	client->sock = accepted;
	INVALIDATE_RANGE(client->union_range);

	client->in_range_ref = (struct sequence_range*) client->in_buf;
	client->in_next = client->in_buf;
	client->in_remain = sizeof(*client->in_range_ref);

	client->out_seq_ref = (sequence*) client->out_buf;
	client->out_id_ref = (identifier*) (client->out_seq_ref + 1);

	sock_set_property(accepted, client);

	if (FAILED(st = clock_time(&client->last_tcp_send)) ||
		FAILED(st = sock_set_nonblock(accepted)) ||
		FAILED(st = poller_add(sender->poller, accepted, POLLIN | POLLOUT)))
		return st;

	sender->last_activity = client->last_tcp_send;

	if (++sender->client_count == 1) {
		st = poller_add(sender->poller, sender->mcast_sock, POLLOUT);
		sender->last_q_idx = storage_get_queue_head(sender->store);
	}

	return st;
}

static status close_sock_func(poller_handle poller, sock_handle sock, short* events, void* param)
{
	struct tcp_client* client = sock_get_property(sock);
	status st;
	(void) events; (void) param;

	if (client) {
		xfree(client->out_buf);
		xfree(client->in_buf);
		xfree(client);
	}

	if (FAILED(st = poller_remove(poller, sock)))
		return st;

	st = sock_close(sock);
	sock_destroy(&sock);
	return st;
}

static status tcp_will_quit_func(poller_handle poller, sock_handle sock, short* events, void* param)
{
	struct tcp_client* client = sock_get_property(sock);
	status st;
	(void) poller; (void) events; (void) param;

	if ( !client)
		return OK;

	*client->out_seq_ref = htonll(WILL_QUIT_SEQ);
	client->out_next = client->out_buf;
	client->out_remain = sizeof(*client->out_seq_ref);

	do {
		st = tcp_write_buf(client);
		if (st != BLOCKED && FAILED(st))
			break;
	} while (client->out_remain > 0);

	return st;
}

static status tcp_on_hup(sender_handle sender, sock_handle sock)
{
	status st;
	if (FAILED(st = close_sock_func(sender->poller, sock, NULL, NULL)))
		return st;

	if (--sender->client_count == 0)
		st = poller_remove(sender->poller, sender->mcast_sock);

	return st;
}

static status tcp_on_write(sender_handle sender, sock_handle sock)
{
	struct tcp_client* client = sock_get_property(sock);
	microsec now;

	status st = tcp_write_buf(client);
	if (st == BLOCKED)
		st = OK;
	else if (st == EOF || st == TIMED_OUT)
		return tcp_on_hup(sender, sock);
	else if (FAILED(st))
		return st;

	if (client->out_remain == 0) {
		if (IS_VALID_RANGE(client->reply_range)) {
			for (; client->reply_id < sender->max_id; ++client->reply_id) {
				sequence seq = sender->slot_seqs[client->reply_id - sender->base_id];
				if (seq < client->min_seq_found)
					client->min_seq_found = seq;

				if (IS_WITHIN_RANGE(client->reply_range, seq)) {
					record_handle rec;
					version ver;
					void* val_at;
					if (FAILED(st = storage_get_record(sender->store, client->reply_id, &rec)))
						return st;

					val_at = record_get_value_ref(rec);
					do {
						ver = record_read_lock(rec);
						memcpy(client->out_id_ref + 1, val_at, sender->val_size);
					} while (ver != record_get_version(rec));

					*client->out_seq_ref = htonll(seq);
					*client->out_id_ref = htonll(client->reply_id);
					client->out_next = client->out_buf;
					client->out_remain = client->pkt_size;

					++client->reply_id;
					return OK;
				}
			}

			sender->min_seq = client->min_seq_found;
			INVALIDATE_RANGE(client->reply_range);
		} else if (!FAILED(st = clock_time(&now)) && (now - client->last_tcp_send) >= sender->heartbeat_usec) {
			*client->out_seq_ref = htonll(HEARTBEAT_SEQ);
			client->out_next = client->out_buf;
			client->out_remain = sizeof(*client->out_seq_ref);
		}
	}

	return st;
}

static status tcp_on_read_blocked(sender_handle sender, sock_handle sock)
{
	struct tcp_client* client = sock_get_property(sock);

	if (client->in_next == client->in_buf) {
		if (client->union_range.high <= sender->min_seq)
			INVALIDATE_RANGE(client->union_range);
		else if (!IS_VALID_RANGE(client->reply_range)) {
			client->reply_id = sender->base_id;
			client->reply_range = client->union_range;
			INVALIDATE_RANGE(client->union_range);
			client->min_seq_found = SEQUENCE_MAX;
		}
	}

	return OK;
}

static status tcp_on_read(sender_handle sender, sock_handle sock)
{
	struct tcp_client* client = sock_get_property(sock);

	status st = tcp_read_buf(client);
	if (st == BLOCKED)
		st = OK;
	else if (st == EOF || st == TIMED_OUT)
		return tcp_on_hup(sender, sock);
	else if (FAILED(st))
		return st;

	if (client->in_remain == 0) {
		client->in_range_ref->low = ntohll(client->in_range_ref->low);
		client->in_range_ref->high = ntohll(client->in_range_ref->high);

		if (!IS_VALID_RANGE(*client->in_range_ref)) {
			errno = EPROTO;
			error_errno("tcp_on_read");
			return BAD_PROTOCOL;
		}

		if (client->in_range_ref->low < client->union_range.low)
			client->union_range.low = client->in_range_ref->low;

		if (client->in_range_ref->high > client->union_range.high)
			client->union_range.high = client->in_range_ref->high;

		client->in_next = client->in_buf;
		client->in_remain = sizeof(*client->in_range_ref);

		SPIN_WRITE_LOCK(&sender->stats.lock, no_ver);
		++sender->stats.tcp_gap_count;
		SPIN_UNLOCK(&sender->stats.lock, no_ver);
	}

	return st;
}

static status event_func(poller_handle poller, sock_handle sock, short* revents, void* param)
{
	sender_handle sender = param;
	status st = OK;
	(void) poller;

	if (sock == sender->listen_sock)
		return tcp_on_accept(sender, sock);

	if (sock == sender->mcast_sock)
		return mcast_on_write(sender);

	if (*revents & POLLHUP)
		return tcp_on_hup(sender, sock);

	if (FAILED(st = (*revents & POLLIN ? tcp_on_read(sender, sock) : tcp_on_read_blocked(sender, sock))))
		return st;

	if (*revents & POLLOUT)
		st = tcp_on_write(sender, sock);

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

status sender_create(sender_handle* psender, const char* mmap_file, microsec heartbeat_usec, long max_pkt_age_usec,
					 const char* mcast_addr, int mcast_port, int mcast_ttl, const char* tcp_addr, int tcp_port)
{
	status st;
	if (!psender || !mmap_file || heartbeat_usec <= 0 || max_pkt_age_usec < 0 ||
		!mcast_addr || mcast_port < 0 || !tcp_addr || tcp_port < 0) {
		error_invalid_arg("sender_create");
		return FAIL;
	}

	*psender = XMALLOC(struct sender);
	if (!*psender)
		return NO_MEMORY;

	BZERO(*psender);

	if (FAILED(st = storage_open(&(*psender)->store, mmap_file, O_RDONLY))) {
		sender_destroy(psender);
		return st;
	}

	SPIN_CREATE(&(*psender)->stats.lock);

	(*psender)->base_id = storage_get_base_id((*psender)->store);
	(*psender)->max_id = storage_get_max_id((*psender)->store);
	(*psender)->val_size = storage_get_value_size((*psender)->store);
	(*psender)->next_seq = 1;
	(*psender)->min_seq = 0;
	(*psender)->max_pkt_age_usec = max_pkt_age_usec;
	(*psender)->heartbeat_usec = heartbeat_usec;

	(*psender)->slot_seqs = xcalloc((*psender)->max_id - (*psender)->base_id, sizeof(sequence));
	if (!(*psender)->slot_seqs) {
		sender_destroy(psender);
		return NO_MEMORY;
	}

	if (FAILED(st = sock_create(&(*psender)->listen_sock, SOCK_STREAM, tcp_addr, tcp_port)) ||
		FAILED(st = sock_set_reuseaddr((*psender)->listen_sock, 1)) ||
		FAILED(st = sock_listen((*psender)->listen_sock, 5)) ||
		(tcp_port == 0 && FAILED(st = sock_update_local_address((*psender)->listen_sock)))) {
		sender_destroy(psender);
		return st;
	}

	if (mcast_port == 0)
		mcast_port = sock_get_port((*psender)->listen_sock);

	if (FAILED(st = sock_create(&(*psender)->mcast_sock, SOCK_DGRAM, mcast_addr, mcast_port)) ||
		FAILED(st = sock_set_nonblock((*psender)->mcast_sock)) ||
		FAILED(st = get_udp_mtu((*psender)->mcast_sock, mcast_addr, &(*psender)->mcast_mtu))) {
		sender_destroy(psender);
		return st;
	}

	st = sprintf((*psender)->hello_str, "%d\r\n%s\r\n%d\r\n%lu\r\n%ld\r\n%ld\r\n%lu\r\n%ld\r\n%ld\r\n",
				 STORAGE_VERSION, mcast_addr, mcast_port, (*psender)->mcast_mtu,
				 (long) (*psender)->base_id, (long) (*psender)->max_id,
				 storage_get_value_size((*psender)->store), max_pkt_age_usec, (*psender)->heartbeat_usec);

	if (st < 0) {
		error_errno("sprintf");
		sender_destroy(psender);
		return FAIL;
	}

	if (FAILED(st = accum_create(&(*psender)->mcast_accum, (*psender)->mcast_mtu, max_pkt_age_usec)) ||
		FAILED(st = sock_set_reuseaddr((*psender)->mcast_sock, 1)) ||
		FAILED(st = sock_mcast_bind((*psender)->mcast_sock)) ||
		FAILED(st = sock_set_mcast_ttl((*psender)->mcast_sock, mcast_ttl)) ||
		FAILED(st = poller_create(&(*psender)->poller, 10)) ||
		FAILED(st = poller_add((*psender)->poller, (*psender)->listen_sock, POLLIN))) {
		sender_destroy(psender);
		return st;
	}

	return OK;
}

void sender_destroy(sender_handle* psender)
{
	if (!psender || !*psender)
		return;

	error_save_last();

	poller_process((*psender)->poller, close_sock_func, *psender);

	poller_destroy(&(*psender)->poller);
	accum_destroy(&(*psender)->mcast_accum);
	storage_destroy(&(*psender)->store);

	error_restore_last();

	xfree((*psender)->slot_seqs);
	xfree(*psender);
	*psender = NULL;
}

storage_handle sender_get_storage(sender_handle sender)
{
	return sender->store;
}

int sender_get_listen_port(sender_handle sender)
{
	return sock_get_port(sender->listen_sock);
}

status sender_run(sender_handle sender)
{
	status st, st2;
	if (FAILED(st = clock_time(&sender->last_mcast_send)))
		return st;

	while (!sender->is_stopping) {
		microsec now;
		if (FAILED(st = poller_events(sender->poller, -1)) ||
			(st > 0 && FAILED(st = poller_process_events(sender->poller, event_func, sender))) ||
			FAILED(st = clock_time(&now)) ||
			((now - sender->last_activity) > IDLE_TIMEOUT_USEC && FAILED(st = clock_sleep(IDLE_SLEEP_USEC))))
			break;
	}

	st2 = poller_process(sender->poller, tcp_will_quit_func, sender);
	if (!FAILED(st))
		st = st2;

	st2 = clock_sleep(1000000);
	if (!FAILED(st))
		st = st2;

	st2 = poller_process(sender->poller, close_sock_func, sender);
	if (!FAILED(st))
		st = st2;

	return st;
}

void sender_stop(sender_handle sender)
{
	sender->is_stopping = TRUE;
}

static size_t get_stat(sender_handle sender, const size_t* pval)
{
	size_t n;
	SPIN_WRITE_LOCK(&sender->stats.lock, no_ver);
	n = *pval;
	SPIN_UNLOCK(&sender->stats.lock, no_ver);
	return n;
}

size_t sender_get_tcp_gap_count(sender_handle sender)
{
	return get_stat(sender, &sender->stats.tcp_gap_count);
}

size_t sender_get_tcp_bytes_sent(sender_handle sender)
{
	return get_stat(sender, &sender->stats.tcp_bytes_sent);
}

size_t sender_get_mcast_bytes_sent(sender_handle sender)
{
	return get_stat(sender, &sender->stats.mcast_bytes_sent);
}

size_t sender_get_mcast_packets_sent(sender_handle sender)
{
	return get_stat(sender, &sender->stats.mcast_packets_sent);
}

size_t sender_get_receiver_count(sender_handle sender)
{
	return sender->client_count;
}
