/* test server */

#include "datum.h"
#include "error.h"
#include "sender.h"
#include "twist.h"
#include "yield.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

int main(int argc, char* argv[])
{
	storage_handle store;
	sender_handle sender;
	twist_handle twister;
	time_t t = time(NULL);
	long tcp_c = 0, mcast_c = 0;
	status st = OK;
	int n = 0, mask;
	const char* mcast_addr, *tcp_addr;
	int mcast_port, tcp_port;
	const char* version;

	if (argc != 6) {
		fprintf(stderr, "Syntax: %s [numeric speed] [mcast address] [mcast port] [tcp address] [tcp port]\n", argv[0]);
		return 1;
	}

	mask = (1 << atoi(argv[1])) - 1;

	mcast_addr = argv[2];
	mcast_port = atoi(argv[3]);

	tcp_addr = argv[4];
	tcp_port = atoi(argv[5]);

	if (FAILED(storage_create(&store, 0, MAX_ID, sizeof(struct datum_t))) ||
		FAILED(sender_create(&sender, store, HB_PERIOD, TRUE, mcast_addr, mcast_port, 1, tcp_addr, tcp_port)) ||
		FAILED(twist_create(&twister)))
		error_report_fatal();

	twist_seed(twister, (unsigned) t);

#ifdef NDEBUG
	version = "RELEASE";
#else
	version = "DEBUG";
#endif

		fprintf(stderr, "data size: %lu bytes (%s build)\n", storage_get_val_size(store) + sizeof(int), version);

	while (sender_is_running(sender) && n < 1000000000) {
		record_handle rec;
		struct datum_t* d;
		time_t t2;

		if (FAILED(st = storage_lookup(store, twist_rand(twister) % MAX_ID, &rec)))
			break;

		d = record_get_val(rec);

		RECORD_LOCK(rec);
		d->bid_qty = ++n;
		RECORD_UNLOCK(rec);

		if (FAILED(st = sender_record_changed(sender, rec)))
			break;

		if ((n & mask) == 0)
			snooze();

		t2 = time(NULL);
		if (t2 != t) {
			long tcp_c2 = sender_get_tcp_bytes_sent(sender);
			long mcast_c2 = sender_get_mcast_bytes_sent(sender);

			fprintf(stderr, "GAPS: %ld TCP: %ld bytes/sec MCAST: %ld bytes/sec          \r",
					sender_get_tcp_gap_count(sender),
					(tcp_c2 - tcp_c) / (t2 - t),
					(mcast_c2 - mcast_c) / (t2 - t));

			t = t2;
			tcp_c = tcp_c2;
			mcast_c = mcast_c2;

			fflush(stderr);
		}
	}

	putchar('\n');

	if ((st != BLOCKED && FAILED(st)) || FAILED(sender_stop(sender)))
		error_report_fatal();

	twist_destroy(&twister);
	sender_destroy(&sender);
	storage_destroy(&store);
	return 0;
}
