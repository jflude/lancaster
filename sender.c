#include "sender.h"
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

#define ORPHAN_TIMEOUT_USEC (3 * 1000000)
#define IDLE_TIMEOUT_USEC 100
#define IDLE_SLEEP_USEC 10

#ifdef DEBUG_PROTOCOL
#include "dump.h"
#include <sys/types.h>
#include <unistd.h>
#endif

struct sender_stats
{
	size_t tcp_gap_count;
	size_t tcp_bytes_sent;
	size_t mcast_bytes_sent;
	size_t mcast_packets_sent;
};

struct sender
{
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
	sequence* record_seqs;
	sequence next_seq;
	sequence min_seq;
	microsec store_created_time;
	microsec mcast_insert_time;
	microsec mcast_send_time;
	microsec last_active_time;
	microsec heartbeat_usec;
	microsec max_pkt_age_usec;
	size_t mcast_mtu;
	char* pkt_buf;
	char* pkt_next;
	q_index last_q_idx;
	struct sender_stats* curr_stats;
	struct sender_stats* next_stats;
	volatile spin_lock stats_lock;
	volatile boolean is_stopping;
	char hello_str[128];
#ifdef DEBUG_PROTOCOL
	FILE* debug_file;
#endif
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

#ifdef DEBUG_PROTOCOL
static const char* debug_time(void)
{
	microsec now;
	static char buf[64];

	if (FAILED(clock_time(&now)) ||
		FAILED(clock_get_text(now, 6, buf, sizeof(buf))))
		error_report_fatal();

	return buf;
}
#endif

static status mcast_send_pkt(sender_handle sndr)
{
	status st;
	microsec now;
	if (FAILED(st = clock_time(&now)))
		return st;

	*((microsec*) (sndr->pkt_buf + sizeof(sequence))) = htonll(now);

	if (FAILED(st = sock_sendto(sndr->mcast_sock, sndr->sendto_addr,
								sndr->pkt_buf, sndr->pkt_next - sndr->pkt_buf)))
		return st;

	sndr->last_active_time = sndr->mcast_send_time = now;

	if (FAILED(st = spin_write_lock(&sndr->stats_lock, NULL)))
		return st;

	sndr->next_stats->mcast_bytes_sent += st;
	++sndr->next_stats->mcast_packets_sent;

	spin_unlock(&sndr->stats_lock, 0);

	sndr->pkt_next = sndr->pkt_buf;
	sndr->mcast_insert_time = 0;

	if (++sndr->next_seq < 0)
		return error_msg("mcast_send_pkt: sequence sign overflow",
						 SIGN_OVERFLOW);
#ifdef DEBUG_PROTOCOL
	fprintf(sndr->debug_file, "%s mcast send seq %07ld\n",
			debug_time(), ntohll(*((sequence*) sndr->pkt_buf)));
#endif
	return st;
}

static status mcast_accum_record(sender_handle sndr, identifier id)
{
	status st;
	revision rev;
	record_handle rec = NULL;
	boolean sent_pkt = FALSE;

	size_t avail_sz = sndr->mcast_mtu - (sndr->pkt_next - sndr->pkt_buf);
	if (avail_sz < (sizeof(identifier) + sndr->val_size)) {
		if (FAILED(st = mcast_send_pkt(sndr)))
			return st;

		sent_pkt = TRUE;
	}
		
	if (sndr->pkt_next == sndr->pkt_buf) {
		*((sequence*) sndr->pkt_buf) = htonll(sndr->next_seq);
		sndr->pkt_next += sizeof(sequence) + sizeof(microsec);
	}

	if (FAILED(st = storage_get_record(sndr->store, id, &rec)))
		return st;

	*((identifier*) sndr->pkt_next) = htonll(id);
	sndr->pkt_next += sizeof(identifier);

	do {
		if (FAILED(st = record_read_lock(rec, &rev)))
			return st;

		memcpy(sndr->pkt_next, record_get_value_ref(rec), sndr->val_size);
	} while (rev != record_get_revision(rec));

	sndr->pkt_next += sndr->val_size;
	sndr->record_seqs[id - sndr->base_id] = sndr->next_seq;

	if (FAILED(st = clock_time(&sndr->mcast_insert_time)))
		return st;

#ifdef DEBUG_PROTOCOL
	fprintf(sndr->debug_file,
			"%s       staging seq %07ld, id #%07ld, rev %07ld, ",
			debug_time(), sndr->next_seq, id, rev);

	fdump(record_get_value_ref(rec), NULL, 16, sndr->debug_file);
#endif
	return sent_pkt;
}

static status mcast_on_write(sender_handle sndr)
{
	status st = OK;
	q_index new_q_idx = storage_get_queue_head(sndr->store);
	q_index qi = new_q_idx - sndr->last_q_idx;

	if (qi < 0) {
		sndr->last_q_idx = new_q_idx;
		qi = 0;
	}

#ifdef DEBUG_PROTOCOL
	fprintf(sndr->debug_file, "%s mcast write %ld queued\n", debug_time(), qi);
#endif

	if (qi > 0) {
		if ((size_t) qi > storage_get_queue_capacity(sndr->store)) {
#ifdef DEBUG_PROTOCOL
			fprintf(sndr->debug_file, "%s mcast queue overrun\n", debug_time());
#endif
			return error_msg("mcast_on_write: change queue overrun",
							 CHANGE_QUEUE_OVERRUN);
		}

		for (qi = sndr->last_q_idx; qi != new_q_idx; ++qi) {
			struct q_element elem;
			if (FAILED(st = storage_read_queue(sndr->store, qi, &elem, TRUE)) ||
				FAILED(st = mcast_accum_record(sndr, elem.id)) || st)
				break;
		}

		sndr->last_q_idx = qi;
		if (st == BLOCKED)
			st = OK;
	} else {
		microsec now;
		if (!FAILED(st = clock_time(&now))) {
			if (sndr->mcast_insert_time != 0 &&
				(now - sndr->mcast_insert_time) >= sndr->max_pkt_age_usec)
				st = mcast_send_pkt(sndr);
			else {
				if ((now - sndr->mcast_send_time) >= sndr->heartbeat_usec) {
					if (sndr->pkt_next == sndr->pkt_buf) {
						*((sequence*) sndr->pkt_buf) = htonll(-sndr->next_seq);
						sndr->pkt_next += sizeof(sequence) + sizeof(microsec);
#ifdef DEBUG_PROTOCOL
						fprintf(sndr->debug_file, "%s mcast heartbeat\n",
								debug_time());
#endif
					}

					st = mcast_send_pkt(sndr);
				}
			}
		}
	}

	return st;
}

static status tcp_read_buf(struct tcp_client* clnt)
{
	status st = OK;
#ifdef DEBUG_PROTOCOL
	size_t recv_sz = 0;
#endif

	while (clnt->in_remain > 0) {
		if (FAILED(st = sock_read(clnt->sock, clnt->in_next, clnt->in_remain)))
			break;

		clnt->in_next += st;
		clnt->in_remain -= st;

#ifdef DEBUG_PROTOCOL
		recv_sz += st;
#endif
	}

#ifdef DEBUG_PROTOCOL
	fprintf(clnt->sndr->debug_file, "%s   tcp recv %lu bytes\n",
			debug_time(), recv_sz);
#endif
	return st;
}

static status tcp_write_buf(struct tcp_client* clnt)
{
	status st = OK;
	size_t sent_sz = 0;

	while (clnt->out_remain > 0) {
		if (FAILED(st = sock_write(clnt->sock, clnt->out_next,
								   clnt->out_remain)))
			break;

		clnt->out_next += st;
		clnt->out_remain -= st;
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

#ifdef DEBUG_PROTOCOL
	fprintf(clnt->sndr->debug_file, "%s   tcp sent %lu bytes\n",
			debug_time(), sent_sz);
#endif
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
		XFREE(clnt);
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
		XFREE(clnt->in_buf);
		XFREE(clnt);
		sock_destroy(&accepted);
		return NO_MEMORY;
	}

	sock_set_property_ref(accepted, clnt);

	if (FAILED(st = clock_time(&clnt->tcp_send_time)) ||
		FAILED(st = sock_set_nonblock(accepted)) ||
		FAILED(st = poller_add(sndr->poller, accepted, POLLIN | POLLOUT)))
		return st;

	sndr->last_active_time = clnt->tcp_send_time;

	if (++sndr->client_count == 1) {
		if (FAILED(st = poller_add(sndr->poller, sndr->mcast_sock, POLLOUT)) ||
			FAILED(st = clock_time(&sndr->mcast_send_time)))
			return st;

		sndr->last_q_idx = storage_get_queue_head(sndr->store);
	}

	return st;
}

static status close_sock_func(poller_handle poller, sock_handle sock,
							  short* events, void* param)
{
	struct tcp_client* clnt = sock_get_property_ref(sock);
	status st;
	(void) events; (void) param;

	if (clnt) {
		XFREE(clnt->out_buf);
		XFREE(clnt->in_buf);
		XFREE(clnt);
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
								 short* events, void* param)
{
	struct tcp_client* clnt = sock_get_property_ref(sock);
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

static status tcp_on_write(sender_handle sndr, sock_handle sock)
{
	struct tcp_client* clnt = sock_get_property_ref(sock);
	microsec now;
	status st;

#ifdef DEBUG_PROTOCOL
	fprintf(sndr->debug_file, "%s   tcp write ready, %lu bytes remain\n",
			debug_time(), clnt->out_remain);
#endif

	st = tcp_write_buf(clnt);
	if (st == BLOCKED)
		st = OK;
	else if (FAILED(st))
		return st;

	if (clnt->out_remain == 0) {
		sequence* out_seq_ref = (sequence*) clnt->out_buf;
		identifier* out_id_ref = (identifier*) (out_seq_ref + 1);

		if (IS_VALID_RANGE(clnt->reply_range)) {
			for (; clnt->reply_id < sndr->max_id; ++clnt->reply_id) {
				sequence seq = sndr->record_seqs[clnt->reply_id - sndr->base_id];
				if (seq < clnt->min_seq_found)
					clnt->min_seq_found = seq;

				if (IS_WITHIN_RANGE(clnt->reply_range, seq)) {
					record_handle rec = NULL;
					revision rev;
					void* val_at;
					if (FAILED(st = storage_get_record(sndr->store,
													   clnt->reply_id, &rec)))
						return st;

					val_at = record_get_value_ref(rec);
					do {
						if (FAILED(st = record_read_lock(rec, &rev)))
							return st;

						if (rev == 0)
							goto next_id;

						memcpy(out_id_ref + 1, val_at, sndr->val_size);
					} while (rev != record_get_revision(rec));

					*out_seq_ref = htonll(seq);
					*out_id_ref = htonll(clnt->reply_id);

					clnt->out_next = clnt->out_buf;
					clnt->out_remain = clnt->pkt_size;
					++clnt->reply_id;
#ifdef DEBUG_PROTOCOL
					fprintf(sndr->debug_file,
							"%s     tcp gap response seq %07ld, id #%07ld\n",
							debug_time(), seq, clnt->reply_id - 1);
#endif
					return OK;
				}
			next_id:;
			}

			sndr->min_seq = clnt->min_seq_found;
			INVALIDATE_RANGE(clnt->reply_range);
		} else if (!FAILED(st = clock_time(&now)) &&
				   (now - clnt->tcp_send_time) >= sndr->heartbeat_usec) {
			*out_seq_ref = htonll(HEARTBEAT_SEQ);
			clnt->out_next = clnt->out_buf;
			clnt->out_remain = sizeof(sequence);

#ifdef DEBUG_PROTOCOL
			fprintf(sndr->debug_file, "%s   tcp heartbeat\n", debug_time());
#endif
		}
	}

	return st;
}

static status tcp_on_read_blocked(sender_handle sndr, sock_handle sock)
{
	struct tcp_client* clnt = sock_get_property_ref(sock);

#ifdef DEBUG_PROTOCOL
	fprintf(sndr->debug_file, "%s   tcp read blocked\n", debug_time());
#endif

	if (clnt->in_next == clnt->in_buf) {
		if (clnt->union_range.high <= sndr->min_seq)
			INVALIDATE_RANGE(clnt->union_range);
		else if (!IS_VALID_RANGE(clnt->reply_range)) {
			clnt->reply_range = clnt->union_range;
			INVALIDATE_RANGE(clnt->union_range);

			clnt->reply_id = sndr->base_id;
			clnt->min_seq_found = SEQUENCE_MAX;

#ifdef DEBUG_PROTOCOL
			fprintf(sndr->debug_file,
					"%s   tcp gap request seq %07ld --> %07ld\n",
					debug_time(), clnt->reply_range.low,
					clnt->reply_range.high);
#endif
		}
	}

	return OK;
}

static status tcp_on_read(sender_handle sndr, sock_handle sock)
{
	struct tcp_client* clnt = sock_get_property_ref(sock);
	status st;

#ifdef DEBUG_PROTOCOL
	fprintf(sndr->debug_file, "%s   tcp read ready, %lu bytes remain\n",
			debug_time(), clnt->in_remain);
#endif

	st = tcp_read_buf(clnt);
	if (st == BLOCKED)
		st = OK;
	else if (FAILED(st))
		return st;

	if (clnt->in_remain == 0) {
		struct sequence_range* r = (struct sequence_range*) clnt->in_buf;
		r->low = ntohll(r->low);
		r->high = ntohll(r->high);

		if (!IS_VALID_RANGE(*r))
			return error_msg("tcp_on_read: invalid sequence range",
							 PROTOCOL_ERROR);

		if (r->low < clnt->union_range.low)
			clnt->union_range.low = r->low;

		if (r->high > clnt->union_range.high)
			clnt->union_range.high = r->high;

		clnt->in_next = clnt->in_buf;
		clnt->in_remain = sizeof(struct sequence_range);

		if (FAILED(st = spin_write_lock(&sndr->stats_lock, NULL)))
			return st;

		++sndr->next_stats->tcp_gap_count;
		spin_unlock(&sndr->stats_lock, 0);
	}

	return st;
}

static status event_func(poller_handle poller, sock_handle sock,
						 short* revents, void* param)
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

	st = (*revents & POLLIN
		  ? tcp_on_read(sndr, sock) : tcp_on_read_blocked(sndr, sock));

	if (!FAILED(st) && *revents & POLLOUT)
		st = tcp_on_write(sndr, sock);

	return FAILED(st) ? tcp_on_hup(sndr, sock) : st;
}

static status init(sender_handle* psndr, const char* mmap_file,
				   const char* tcp_address, unsigned short tcp_port,
				   const char* mcast_address, unsigned short mcast_port,
				   const char* mcast_interface, short mcast_ttl,
				   boolean mcast_loopback, microsec heartbeat_usec,
				   microsec max_pkt_age_usec)
{
	status st;
#ifdef DEBUG_PROTOCOL
	char debug_name[256];
#endif

	BZERO(*psndr);

	if (FAILED(st = storage_open(&(*psndr)->store, mmap_file, O_RDONLY)))
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
	(*psndr)->max_pkt_age_usec = max_pkt_age_usec;
	(*psndr)->heartbeat_usec = heartbeat_usec;

	if (FAILED(st = storage_get_created_time((*psndr)->store,
											 &(*psndr)->store_created_time)))
		return st;

	(*psndr)->record_seqs = xcalloc((*psndr)->max_id - (*psndr)->base_id,
									sizeof(sequence));
	if (!(*psndr)->record_seqs)
		return NO_MEMORY;

	if (FAILED(st = sock_create(&(*psndr)->listen_sock,
								SOCK_STREAM, IPPROTO_TCP)) ||
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

	if (FAILED(st = sock_create(&(*psndr)->mcast_sock,
								SOCK_DGRAM, IPPROTO_UDP)) ||
		FAILED(st = sock_set_nonblock((*psndr)->mcast_sock)))
		return st;

	if (!mcast_interface)
		(*psndr)->mcast_mtu = DEFAULT_MTU;
	else {
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

	(*psndr)->pkt_buf = xmalloc((*psndr)->mcast_mtu);
	if (!(*psndr)->pkt_buf)
		return NO_MEMORY;

	(*psndr)->pkt_next = (*psndr)->pkt_buf;

	st = sprintf((*psndr)->hello_str,
				 WIRE_VERSION
				 "\r\n%hu\r\n%s\r\n%d\r\n%lu\r\n"
				 "%ld\r\n%ld\r\n%lu\r\n%ld\r\n%ld\r\n",
				 storage_get_data_version((*psndr)->store),
				 mcast_address,
				 mcast_port,
				 (*psndr)->mcast_mtu,
				 (long) (*psndr)->base_id,
				 (long) (*psndr)->max_id,
				 storage_get_value_size((*psndr)->store),
				 max_pkt_age_usec,
				 (*psndr)->heartbeat_usec);

	if (st < 0)
		return error_errno("sprintf");

	if (FAILED(st = sock_set_reuseaddr((*psndr)->mcast_sock, TRUE)) ||
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

#ifdef DEBUG_PROTOCOL
	sprintf(debug_name, "SEND-%s-%d-%d.DEBUG", tcp_address,
			(int) sock_addr_get_port((*psndr)->listen_addr), (int) getpid());

	(*psndr)->debug_file = fopen(debug_name, "w");
	if (!(*psndr)->debug_file)
		st = error_errno("fopen");
#endif

	return st;
}

status sender_create(sender_handle* psndr, const char* mmap_file,
					 const char* tcp_address, unsigned short tcp_port,
					 const char* mcast_address, unsigned short mcast_port,
					 const char* mcast_interface, short mcast_ttl,
					 boolean mcast_loopback, microsec heartbeat_usec,
					 microsec max_pkt_age_usec)
{
	status st;
	if (!psndr || !mmap_file || heartbeat_usec <= 0 ||
		max_pkt_age_usec < 0 || !mcast_address || !tcp_address)
		return error_invalid_arg("sender_create");

	*psndr = XMALLOC(struct sender);
	if (!*psndr)
		return NO_MEMORY;

	if (FAILED(st = init(psndr, mmap_file, tcp_address, tcp_port,
						 mcast_address, mcast_port, mcast_interface,
						 mcast_ttl, mcast_loopback, heartbeat_usec,
						 max_pkt_age_usec))) {
		error_save_last();
		sender_destroy(psndr);
		error_restore_last();
	}

	return st;
}

status sender_destroy(sender_handle* psndr)
{
	status st = OK;
	if (!psndr || !*psndr ||
		((*psndr)->poller && FAILED(st = poller_process((*psndr)->poller,
														close_sock_func,
														*psndr))) ||
		FAILED(st = poller_destroy(&(*psndr)->poller)) ||
		FAILED(st = storage_destroy(&(*psndr)->store)) ||
		FAILED(st = sock_addr_destroy(&(*psndr)->sendto_addr)) ||
		FAILED(st = sock_addr_destroy(&(*psndr)->listen_addr)))
		return st;

	XFREE((*psndr)->pkt_buf);
	XFREE((*psndr)->record_seqs);
	XFREE((*psndr)->next_stats);
	XFREE((*psndr)->curr_stats);

#ifdef DEBUG_PROTOCOL
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
#ifdef DEBUG_PROTOCOL
		fprintf(sndr->debug_file, "%s ======================================\n",
				debug_time());
#endif
		if (FAILED(st = poller_events(sndr->poller, 0)) ||
			(st > 0 && FAILED(st = poller_process_events(sndr->poller,
														 event_func, sndr))) ||
			FAILED(st = clock_time(&now)) ||
			FAILED(st = storage_get_touched_time(sndr->store, &when)))
			break;

		when = now - when;
		if (when >= ORPHAN_TIMEOUT_USEC) {
			st = error_msg("sender_run: storage is orphaned", STORAGE_ORPHANED);
			break;
		}

		if (FAILED(st = storage_get_created_time(sndr->store, &when)))
			break;

		if (when != sndr->store_created_time) {
			st = error_msg("sender_run: storage is recreated",
						   STORAGE_RECREATED);
			break;
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

size_t sender_get_tcp_gap_count(sender_handle sndr)
{
	return sndr->curr_stats->tcp_gap_count;
}

size_t sender_get_tcp_bytes_sent(sender_handle sndr)
{
	return sndr->curr_stats->tcp_bytes_sent;
}

size_t sender_get_mcast_bytes_sent(sender_handle sndr)
{
	return sndr->curr_stats->mcast_bytes_sent;
}

size_t sender_get_mcast_packets_sent(sender_handle sndr)
{
	return sndr->curr_stats->mcast_packets_sent;
}

size_t sender_get_receiver_count(sender_handle sndr)
{
	return sndr->client_count;
}

status sender_next_stats(sender_handle sndr)
{
	status st;
	struct sender_stats* tmp;

	if (FAILED(st = spin_write_lock(&sndr->stats_lock, NULL)))
		return st;

	tmp = sndr->next_stats;
	sndr->next_stats = sndr->curr_stats;
	sndr->curr_stats = tmp;

	BZERO(sndr->next_stats);

	spin_unlock(&sndr->stats_lock, 0);
	return st;
}
