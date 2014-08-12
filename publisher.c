/* test publisher */

#include "clock.h"
#include "datum.h"
#include "error.h"
#include "sender.h"
#include "signals.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DISPLAY_DELAY_USEC 1000000
#define SCATTER_UPDATES

#ifdef SCATTER_UPDATES
#include "twist.h"
static twist_handle twister;
#endif

static storage_handle store;
static sender_handle sender;
static size_t pkt1, tcp1, mcast1;
static microsec_t last_print;
static int delay;
static boolean verbose;

static void syntax(const char* prog)
{
	fprintf(stderr, "Syntax: %s [-v|--verbose] [delay] [heartbeat interval] "
			"[multicast address] [multicast port] [TCP address] [TCP port]\n", prog);
	exit(EXIT_FAILURE);
}

static status update(identifier id, long n)
{
	record_handle rec;
	struct datum_t* d;
	sequence seq;
	microsec_t now;
	status st = OK;

	if (!sender_is_running(sender) || signal_is_raised(SIGINT) || signal_is_raised(SIGTERM))
		return FALSE;

	if (FAILED(st = storage_get_record(store, id, &rec)) || FAILED(st = clock_time(&now)))
		return st;

	d = record_get_value_ref(rec);
	seq = record_write_lock(rec);

	d->xyz = n;
	d->ts = now;

	record_set_sequence(rec, seq);
	if (FAILED(st = sender_record_changed(sender, rec)))
		return st;

	if (verbose) {
		microsec_t elapsed = now - last_print;
		if (elapsed >= DISPLAY_DELAY_USEC) {
			double secs = elapsed / 1000000.0;
			size_t pkt2 = sender_get_mcast_packets_sent(sender);
			size_t tcp2 = sender_get_tcp_bytes_sent(sender);
			size_t mcast2 = sender_get_mcast_bytes_sent(sender);

			printf("\"%.8s\", SUBS: %lu, PKTS/sec: %.2f, GAPS: %lu, TCP KB/sec: %.2f, MCAST KB/sec: %.2f            \r",
				   storage_get_description(store),
				   sender_get_subscriber_count(sender),
				   (pkt2 - pkt1) / secs,
				   sender_get_tcp_gap_count(sender),
				   (tcp2 - tcp1) / secs / 1024,
				   (mcast2 - mcast1) / secs / 1024);

			fflush(stdout);

			last_print = now;
			pkt1 = pkt2;
			tcp1 = tcp2;
			mcast1 = mcast2;
		}
	}

	if (delay > 0 && FAILED(st = clock_sleep(delay)))
		return st;

	return TRUE;
}

int main(int argc, char* argv[])
{
	status st = OK;
	int hb, n = 1;
	long xyz;
	const char* mcast_addr, *tcp_addr;
	int mcast_port, tcp_port;

	if (argc < 7 || argc > 8)
		syntax(argv[0]);

	if (strcmp(argv[n], "-v") == 0 || strcmp(argv[n], "--verbose") == 0) {
		if (argc != 8)
			syntax(argv[0]);

		verbose = TRUE;
		n++;
	}

	delay = atoi(argv[n++]);
	hb = atoi(argv[n++]);
	mcast_addr = argv[n++];
	mcast_port = atoi(argv[n++]);
	tcp_addr = argv[n++];
	tcp_port = atoi(argv[n++]);

	if (FAILED(signal_add_handler(SIGINT)) || FAILED(signal_add_handler(SIGTERM)) ||
		FAILED(storage_create(&store, NULL, 0, 0, 0, MAX_ID, sizeof(struct datum_t))) ||
		FAILED(storage_set_description(store, "TEST")) || FAILED(storage_reset(store)) ||
		FAILED(sender_create(&sender, store, hb, MAX_AGE_USEC, CONFLATE_PKT,
							 mcast_addr, mcast_port, 64, tcp_addr, tcp_port)) ||
		FAILED(clock_time(&last_print)))
		error_report_fatal();

#ifdef SCATTER_UPDATES
	if (FAILED(st = twist_create(&twister)))
		return st;

	twist_seed(twister, (unsigned) last_print);
#endif

	pkt1 = sender_get_mcast_packets_sent(sender);
	tcp1 = sender_get_tcp_bytes_sent(sender);
	mcast1 = sender_get_mcast_bytes_sent(sender);

	xyz = 0;
	for (;;) {
		identifier id;
#ifdef SCATTER_UPDATES
		id = twist_rand(twister) % MAX_ID;
#else
		for (id = 0; id < MAX_ID; ++id)
#endif
			if (FAILED(st = update(id, xyz++)) || !st)
				goto finish;
	}

finish:
	if (verbose)
		putchar('\n');

	if (FAILED(st) || FAILED(sender_stop(sender)))
		error_report_fatal();

	sender_destroy(&sender);
	storage_destroy(&store);

#ifdef SCATTER_UPDATES
	twist_destroy(&twister);
#endif

	if (FAILED(signal_remove_handler(SIGINT)) || FAILED(signal_remove_handler(SIGTERM)))
		error_report_fatal();

	return 0;
}
