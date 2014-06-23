/* test publisher */

#include "datum.h"
#include "error.h"
#include "sender.h"
#include "yield.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

int main(int argc, char* argv[])
{
	storage_handle store;
	sender_handle sender;
	status st = OK;
	int mask, n = 0, i = 1;
	const char* mcast_addr, *tcp_addr;
	int mcast_port, tcp_port;
	int verbose = 0;
	time_t t1 = time(NULL);
	long tcp_c = 0, mcast_c = 0;

	if (argc < 6 || argc > 7) {
		fprintf(stderr, "Syntax: %s [-v|--verbose] [speed] [mcast address] [mcast port] [tcp address] [tcp port]\n", argv[0]);
		return 1;
	}

	if (strcmp(argv[i], "-v") == 0 || strcmp(argv[1], "--verbose") == 0) {
		verbose = 1;
		i++;
	}

	mask = (1 << atoi(argv[i++])) - 1;
	mcast_addr = argv[i++];
	mcast_port = atoi(argv[i++]);
	tcp_addr = argv[i++];
	tcp_port = atoi(argv[i++]);

	if (FAILED(storage_create(&store, NULL, 0, 0, MAX_ID, sizeof(struct datum_t))) ||
		FAILED(sender_create(&sender, store, HB_PERIOD, TRUE, mcast_addr, mcast_port, 1, tcp_addr, tcp_port)))
		error_report_fatal();

	while (sender_is_running(sender) && n < 1000000000) {
		for (i = 0; i < MAX_ID; ++i) {
			record_handle rec;
			struct datum_t* d;

			if (FAILED(st = storage_lookup(store, i, &rec)))
				goto finish;

			d = record_get_value(rec);

			RECORD_LOCK(rec);
			d->bid_qty = ++n;
			RECORD_UNLOCK(rec);

			if (FAILED(st = sender_record_changed(sender, rec)))
				goto finish;

			if ((n & mask) == 0)
				snooze();
		}

		if (verbose) {
			time_t t2 = time(NULL);
			if (t2 != t1) {
				long tcp_c2 = sender_get_tcp_bytes_sent(sender);
				long mcast_c2 = sender_get_mcast_bytes_sent(sender);

				printf("GAPS: %ld TCP: %ld bytes/sec MCAST: %ld bytes/sec          \r",
						sender_get_tcp_gap_count(sender),
						(tcp_c2 - tcp_c) / (t2 - t1),
						(mcast_c2 - mcast_c) / (t2 - t1));

				t1 = t2;
				tcp_c = tcp_c2;
				mcast_c = mcast_c2;
				
				fflush(stdout);
			}
		}
	}

finish:
	if (verbose)
		putchar('\n');

	if ((st != BLOCKED && FAILED(st)) || FAILED(sender_stop(sender)))
		error_report_fatal();

	sender_destroy(&sender);
	storage_destroy(&store);
	return 0;
}
