/* test publisher */

#include "clock.h"
#include "datum.h"
#include "error.h"
#include "sender.h"
#include "signals.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void syntax(const char* prog)
{
	fprintf(stderr, "Syntax: %s [-v|--verbose] [delay] [heartbeat interval] "
			"[multicast address] [multicast port] [TCP address] [TCP port]\n", prog);
	exit(EXIT_FAILURE);
}

int main(int argc, char* argv[])
{
	storage_handle store = NULL;
	sender_handle sender;
	status st = OK;
	int delay, hb, n = 1;
	identifier id;
	const char* mcast_addr, *tcp_addr;
	int mcast_port, tcp_port;
	boolean verbose = FALSE;
	size_t pkt_c, tcp_c, mcast_c;
	time_t t1;

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

	if (FAILED(signal_add_handler(SIGINT)) ||
		FAILED(signal_add_handler(SIGTERM)) ||
		FAILED(storage_create(&store, NULL, 0, 0, 0, MAX_ID, sizeof(struct datum_t))) ||
		FAILED(storage_set_description(store, "TEST")) ||
		FAILED(storage_reset(store)) ||
		FAILED(sender_create(&sender, store, hb, MAX_AGE_USEC, CONFLATE_PKT,
							 mcast_addr, mcast_port, 64, tcp_addr, tcp_port)))
		error_report_fatal();

	t1 = time(NULL);
	pkt_c = sender_get_mcast_packets_sent(sender);
	tcp_c = sender_get_tcp_bytes_sent(sender);
	mcast_c = sender_get_mcast_bytes_sent(sender);

	n = 0;
	for (;;)
		for (id = 0; id < MAX_ID; ++id) {
			record_handle rec;
			struct datum_t* d;
			sequence seq;

			if (!sender_is_running(sender) || signal_is_raised(SIGINT) || signal_is_raised(SIGTERM))
				goto finish;

			if (FAILED(st = storage_get_record(store, id, &rec)))
				goto finish;

			d = record_get_value_ref(rec);

			seq = record_write_lock(rec);
			d->bidSize = n++;
			d->askSize = n++;
			record_set_sequence(rec, seq);

			if (FAILED(st = sender_record_changed(sender, rec)))
				goto finish;

			if (verbose) {
				time_t t2 = time(NULL);
				if (t2 != t1) {
					time_t elapsed = t2 - t1;
					size_t pkt_c2 = sender_get_mcast_packets_sent(sender);
					size_t tcp_c2 = sender_get_tcp_bytes_sent(sender);
					size_t mcast_c2 = sender_get_mcast_bytes_sent(sender);

					printf("SUBS: %lu, PKTS/sec: %ld, GAPS: %lu, TCP KB/sec: %.2f, MCAST KB/sec: %.2f            \r",
						   sender_get_subscriber_count(sender),
						   (pkt_c2 - pkt_c) / elapsed,
						   sender_get_tcp_gap_count(sender),
						   (tcp_c2 - tcp_c) / 1024.0 / elapsed,
						   (mcast_c2 - mcast_c) / 1024.0 / elapsed);
				
					fflush(stdout);

					t1 = t2;
					pkt_c = pkt_c2;
					tcp_c = tcp_c2;
					mcast_c = mcast_c2;
				}
			}

			if (delay > 0 && FAILED(st = clock_sleep(delay)))
				goto finish;
		}

finish:
	if (verbose)
		putchar('\n');

	if (FAILED(st) ||
		FAILED(sender_stop(sender)))
		error_report_fatal();

	sender_destroy(&sender);
	storage_destroy(&store);

	if (FAILED(signal_remove_handler(SIGINT)) ||
		FAILED(signal_remove_handler(SIGTERM)))
		error_report_fatal();

	return 0;
}
