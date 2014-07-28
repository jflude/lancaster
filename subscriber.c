/* generic subscriber */

#include "error.h"
#include "receiver.h"
#include "signals.h"
#include "yield.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

int main(int argc, char* argv[])
{
	receiver_handle recv;
	const char* tcp_addr;
	int tcp_port;
	const char* storage_file;
	int q_capacity, n = 1;
	boolean verbose = FALSE;
	size_t pkt_c, tcp_c, mcast_c;
	time_t t1;

	if (argc < 5 || argc > 6) {
		fprintf(stderr, "Syntax: %s [-v|--verbose] [tcp address] [tcp port] [change queue size] [storage file]\n", argv[0]);
		return 1;
	}

	if (strcmp(argv[n], "-v") == 0 || strcmp(argv[n], "--verbose") == 0) {
		verbose = TRUE;
		n++;
	}

	tcp_addr = argv[n++];
	tcp_port = atoi(argv[n++]);
	q_capacity = atoi(argv[n++]);
	storage_file = argv[n++];

	if (FAILED(signal_add_handler(SIGINT)) || FAILED(signal_add_handler(SIGTERM)) ||
		FAILED(receiver_create(&recv, storage_file, q_capacity, tcp_addr, tcp_port)))
		error_report_fatal();

	t1 = time(NULL);
	pkt_c = receiver_get_mcast_packets_recv(recv);
	tcp_c = receiver_get_tcp_bytes_recv(recv);
	mcast_c = receiver_get_mcast_bytes_recv(recv);

	while (receiver_is_running(recv) && !signal_is_raised(SIGINT) && !signal_is_raised(SIGTERM)) {
		if (verbose) {
			time_t t2 = time(NULL);
			if (t2 != t1) {
				time_t elapsed = t2 - t1;
				size_t pkt_c2 = receiver_get_mcast_packets_recv(recv);
				size_t tcp_c2 = receiver_get_tcp_bytes_recv(recv);
				size_t mcast_c2 = receiver_get_mcast_bytes_recv(recv);

				printf("PKTS/sec: %ld, GAPS: %lu, TCP KB/sec: %.2f, MCAST KB/sec: %.2f, "
					   "MIN/us: %.2f, AVG/us: %.2f, MAX/us: %.2f, STD/us: %.2f         \r",
					   (pkt_c2 - pkt_c) / elapsed,
					   receiver_get_tcp_gap_count(recv),
					   (tcp_c2 - tcp_c) / 1024.0 / elapsed,
					   (mcast_c2 - mcast_c) / 1024.0 / elapsed,
					   receiver_get_mcast_min_latency(recv) / 1000.0,
					   receiver_get_mcast_mean_latency(recv) / 1000.0,
					   receiver_get_mcast_max_latency(recv) / 1000.0,
					   receiver_get_mcast_stddev_latency(recv) / 1000.0);

				t1 = t2;
				pkt_c = pkt_c2;
				tcp_c = tcp_c2;
				mcast_c = mcast_c2;

				fflush(stdout);
			}
		}

		snooze(1, 0);
	}

	if (verbose)
		putchar('\n');

	if (FAILED(receiver_stop(recv)))
		error_report_fatal();

	receiver_destroy(&recv);

	if (FAILED(signal_remove_handler(SIGINT)) || FAILED(signal_remove_handler(SIGTERM)))
		error_report_fatal();
	
	return 0;
}
