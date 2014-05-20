/* test client */

#include "datum.h"
#include "error.h"
#include "receiver.h"
#include "yield.h"
#include <stdio.h>
#include <time.h>

int main()
{
	receiver_handle recv;
	time_t t = time(NULL);
	long tcp_c = 0, mcast_c = 0;
	status st = OK;

	if (FAILED(receiver_create(&recv, Q_CAPACITY, HB_RECV_PERIOD, TCP_ADDR, TCP_PORT)))
		error_report_fatal();

	while (receiver_is_running(recv)) {
		record_handle rec;
		time_t t2;

		st = receiver_record_changed(recv, &rec);
		if (st == BLOCKED) {
			yield();

			t2 = time(NULL);
			if (t2 != t) {
				long tcp_c2 = receiver_get_tcp_bytes_recv(recv);
				long mcast_c2 = receiver_get_mcast_bytes_recv(recv);
				
				fprintf(stderr, "GAPS: %ld TCP: %ld bytes/sec MCAST: %ld bytes/sec        \r",
					   receiver_get_tcp_gap_count(recv),
					   (tcp_c2 - tcp_c) / (t2 - t),
					   (mcast_c2 - mcast_c) / (t2 - t));
				
				t = t2;
				tcp_c = tcp_c2;
				mcast_c = mcast_c2;
				
				fflush(stderr);
			}

			continue;
		} else if (FAILED(st))
			break;

		/* RECORD_LOCK(rec), do something with updated *rec, RECORD_UNLOCK(rec) */
	}

	putchar('\n');

	if ((st != BLOCKED && FAILED(st)) || FAILED(receiver_stop(recv)))
		error_report_fatal();

	receiver_destroy(&recv);
	return 0;
}
