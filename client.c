/* test client */

#include "datum.h"
#include "error.h"
#include "receiver.h"
#include "yield.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

int main(int argc, char* argv[])
{
	receiver_handle recv;
	time_t t = time(NULL);
	long tcp_c = 0, mcast_c = 0;
	status st = OK;
	const char* tcp_addr;
	int tcp_port;
	const char* version;

	if (argc != 3) {
		fprintf(stderr, "Syntax: %s [tcp address] [tcp port]\n", argv[0]);
		return 1;
	}

	tcp_addr = argv[1];
	tcp_port = atoi(argv[2]);

	if (FAILED(receiver_create(&recv, Q_CAPACITY, tcp_addr, tcp_port)))
		error_report_fatal();

#ifdef NDEBUG
	version = "RELEASE";
#else
	version = "DEBUG";
#endif

	fprintf(stderr, "data size: %lu bytes (%s build)\n", storage_get_val_size(receiver_get_storage(recv)) + sizeof(int), version);

	while (receiver_is_running(recv)) {
		record_handle rec;
		struct datum_t* d;
		time_t t2;
		int v;

		st = receiver_record_changed(recv, &rec);
		if (st == BLOCKED) {
			snooze();
			continue;
		} else if (FAILED(st))
			break;

		d = record_get_val(rec);

		RECORD_LOCK(rec);
		v = d->bid_qty;
		RECORD_UNLOCK(rec);

		(void) v;

		t2 = time(NULL);
		if (t2 != t) {
			long tcp_c2 = receiver_get_tcp_bytes_recv(recv);
			long mcast_c2 = receiver_get_mcast_bytes_recv(recv);

			fprintf(stderr, "GAPS: %ld TCP: %ld bytes/sec MCAST: %ld bytes/sec QUEUED: %u          \r",
					receiver_get_tcp_gap_count(recv),
					(tcp_c2 - tcp_c) / (t2 - t),
					(mcast_c2 - mcast_c) / (t2 - t),
					receiver_get_queue_count(recv));

			t = t2;
			tcp_c = tcp_c2;
			mcast_c = mcast_c2;

			fflush(stderr);
		}
	}

	putchar('\n');

	if ((st != BLOCKED && FAILED(st)) || FAILED(receiver_stop(recv)))
		error_report_fatal();

	receiver_destroy(&recv);
	return 0;
}
