/* generic subscriber */

#include "error.h"
#include "receiver.h"
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
	int q_capacity;
	int verbose = 0;
	int i = 1;
	time_t t1 = time(NULL);
	size_t tcp_c = 0, mcast_c = 0;

	if (argc < 5 || argc > 6) {
		fprintf(stderr, "Syntax: %s [-v|--verbose] [tcp address] [tcp port] [change queue size] [storage file]\n", argv[0]);
		return 1;
	}

	if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
		verbose = 1;
		i++;
	}

	tcp_addr = argv[i++];
	tcp_port = atoi(argv[i++]);
	q_capacity = atoi(argv[i++]);
	storage_file = argv[i++];

	if (FAILED(receiver_create(&recv, storage_file, q_capacity, tcp_addr, tcp_port)))
		error_report_fatal();

	while (receiver_is_running(recv)) {
		if (verbose) {
			time_t t2 = time(NULL);
			if (t2 != t1) {
				size_t tcp_c2 = receiver_get_tcp_bytes_recv(recv);
				size_t mcast_c2 = receiver_get_mcast_bytes_recv(recv);

				printf("GAPS: %ld TCP: %ld bytes/sec MCAST: %ld bytes/sec          \r",
					   receiver_get_tcp_gap_count(recv),
					   (tcp_c2 - tcp_c) / (t2 - t1),
					   (mcast_c2 - mcast_c) / (t2 - t1));

				t1 = t2;
				tcp_c = tcp_c2;
				mcast_c = mcast_c2;

				fflush(stdout);
			}
		}

		slumber(1);
	}

	if (verbose)
		putchar('\n');

	if (FAILED(receiver_stop(recv)))
		error_report_fatal();

	receiver_destroy(&recv);
	return 0;
}
