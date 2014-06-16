/* test subscriber */

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
	const char* tcp_addr;
	int tcp_port;
	const char* version;

	if (argc != 3) {
		fprintf(stderr, "Syntax: %s [tcp address] [tcp port]\n", argv[0]);
		return 1;
	}

	tcp_addr = argv[1];
	tcp_port = atoi(argv[2]);

	if (FAILED(receiver_create(&recv, STORAGE_FILE, Q_CAPACITY, tcp_addr, tcp_port)))
		error_report_fatal();

#ifdef NDEBUG
	version = "RELEASE";
#else
	version = "DEBUG";
#endif

	fprintf(stderr, "data size: %lu bytes (%s build)\n",
			storage_get_value_size(receiver_get_storage(recv)) + sizeof(int),
			version);

	while (receiver_is_running(recv)) {
		time_t t2 = time(NULL);
		if (t2 != t) {
			long tcp_c2 = receiver_get_tcp_bytes_recv(recv);
			long mcast_c2 = receiver_get_mcast_bytes_recv(recv);

			fprintf(stderr, "GAPS: %ld TCP: %ld bytes/sec MCAST: %ld bytes/sec          \r",
					receiver_get_tcp_gap_count(recv),
					(tcp_c2 - tcp_c) / (t2 - t),
					(mcast_c2 - mcast_c) / (t2 - t));

			t = t2;
			tcp_c = tcp_c2;
			mcast_c = mcast_c2;

			fflush(stderr);
		}

		slumber(1);
	}

	putchar('\n');

	if (FAILED(receiver_stop(recv)))
		error_report_fatal();

	receiver_destroy(&recv);
	return 0;
}
