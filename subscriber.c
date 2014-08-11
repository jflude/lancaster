/* generic subscriber */

#include "error.h"
#include "clock.h"
#include "receiver.h"
#include "signals.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DISPLAY_DELAY_USEC 1000000

static void syntax(const char* prog)
{
	fprintf(stderr, "Syntax: %s [-v|--verbose] [TCP address] [TCP port] "
			"[change queue size] [storage file or segment]\n", prog);
	exit(EXIT_FAILURE);
}

int main(int argc, char* argv[])
{
	receiver_handle recv;
	status st = OK;
	const char* tcp_addr;
	int tcp_port;
	const char* storage_file;
	int q_capacity, n = 1;
	boolean verbose = FALSE;
	size_t pkt_c, tcp_c, mcast_c;
	microsec_t last_print;

	if (argc < 5 || argc > 6)
		syntax(argv[0]);

	if (strcmp(argv[n], "-v") == 0 || strcmp(argv[n], "--verbose") == 0) {
		if (argc != 6)
			syntax(argv[0]);

		verbose = TRUE;
		n++;
	}

	tcp_addr = argv[n++];
	tcp_port = atoi(argv[n++]);
	q_capacity = atoi(argv[n++]);
	storage_file = argv[n++];

	if (FAILED(signal_add_handler(SIGINT)) ||
		FAILED(signal_add_handler(SIGTERM)) ||
		FAILED(receiver_create(&recv, storage_file, q_capacity, tcp_addr, tcp_port)) ||
		FAILED(clock_time(&last_print)))
		error_report_fatal();

	pkt_c = receiver_get_mcast_packets_recv(recv);
	tcp_c = receiver_get_tcp_bytes_recv(recv);
	mcast_c = receiver_get_mcast_bytes_recv(recv);

	while (receiver_is_running(recv) && !signal_is_raised(SIGINT) && !signal_is_raised(SIGTERM)) {
		if (verbose) {
			microsec_t now, elapsed;
			if (FAILED(st = clock_time(&now)))
				break;

			elapsed = now - last_print;
			if (elapsed >= DISPLAY_DELAY_USEC) {
				double secs = elapsed / 1000000.0;
				size_t pkt_c2 = receiver_get_mcast_packets_recv(recv);
				size_t tcp_c2 = receiver_get_tcp_bytes_recv(recv);
				size_t mcast_c2 = receiver_get_mcast_bytes_recv(recv);

				printf("\"%.8s\", PKTS/sec: %.2f, GAPS: %lu, TCP KB/sec: %.2f, MCAST KB/sec: %.2f, "
					   "MIN/us: %.2f, AVG/us: %.2f, MAX/us: %.2f, STD/us: %.2f           \r",
					   storage_get_description(receiver_get_storage(recv)),
					   (pkt_c2 - pkt_c) / secs,
					   receiver_get_tcp_gap_count(recv),
					   (tcp_c2 - tcp_c) / secs / 1024,
					   (mcast_c2 - mcast_c) / secs / 1024,
					   receiver_get_mcast_min_latency(recv),
					   receiver_get_mcast_mean_latency(recv),
					   receiver_get_mcast_max_latency(recv),
					   receiver_get_mcast_stddev_latency(recv));

				fflush(stdout);

				last_print = now;
				pkt_c = pkt_c2;
				tcp_c = tcp_c2;
				mcast_c = mcast_c2;
			}
		}

		if (FAILED(st = clock_sleep(1000000)))
			break;
	}

	if (verbose)
		putchar('\n');

	if (FAILED(st) ||
		FAILED(receiver_stop(recv)))
		error_report_fatal();

	receiver_destroy(&recv);

	if (FAILED(signal_remove_handler(SIGINT)) ||
		FAILED(signal_remove_handler(SIGTERM)))
		error_report_fatal();
	
	return 0;
}
