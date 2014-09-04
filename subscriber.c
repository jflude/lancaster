/* generic subscriber */

#include "error.h"
#include "clock.h"
#include "receiver.h"
#include "signals.h"
#include "thread.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DISPLAY_DELAY_USEC 1000000

static void syntax(const char* prog)
{
	fprintf(stderr, "Syntax: %s [storage file or segment] [change queue size] "
			"[TCP address] [TCP port]\n", prog);
	exit(EXIT_FAILURE);
}

static void* stats_func(thread_handle thr)
{
	receiver_handle recv = thread_get_param(thr);
	microsec last_print;
	status st;

	size_t pkt1 = receiver_get_mcast_packets_recv(recv);
	size_t tcp1 = receiver_get_tcp_bytes_recv(recv);
	size_t mcast1 = receiver_get_mcast_bytes_recv(recv);

	if (FAILED(st = clock_time(&last_print)))
		return (void*) (long) st;

	while (!thread_is_stopping(thr)) {
		double secs;
		size_t pkt2, tcp2, mcast2;
		microsec now, elapsed;

		if (FAILED(st = clock_sleep(DISPLAY_DELAY_USEC)) ||
			FAILED(st = clock_time(&now)))
			break;

		elapsed = now - last_print;
		secs = elapsed / 1000000.0;
		pkt2 = receiver_get_mcast_packets_recv(recv);
		tcp2 = receiver_get_tcp_bytes_recv(recv);
		mcast2 = receiver_get_mcast_bytes_recv(recv);

		printf("\"%.16s\", PKT/s: %.2f, GAP: %lu, "
			   "TCP KB/s: %.2f, MCAST KB/s: %.2f, "
			   "MIN/us: %.2f, AVG/us: %.2f, MAX/us: %.2f, "
			   "STD/us: %.2f        \r",
			   storage_get_description(receiver_get_storage(recv)),
			   (pkt2 - pkt1) / secs,
			   receiver_get_tcp_gap_count(recv),
			   (tcp2 - tcp1) / secs / 1024,
			   (mcast2 - mcast1) / secs / 1024,
			   receiver_get_mcast_min_latency(recv),
			   receiver_get_mcast_mean_latency(recv),
			   receiver_get_mcast_max_latency(recv),
			   receiver_get_mcast_stddev_latency(recv));

		fflush(stdout);

		last_print = now;
		pkt1 = pkt2;
		tcp1 = tcp2;
		mcast1 = mcast2;
	}

	putchar('\n');
	return (void*) (long) st;
}

int main(int argc, char* argv[])
{
	receiver_handle recv;
	thread_handle stats_thread;
	const char *mmap_file, *tcp_addr;
	int tcp_port;
	size_t q_capacity;
	int n = 1;
	status st;

	error_set_program_name(argv[0]);

	if (argc != 5)
		syntax(argv[0]);

	mmap_file = argv[n++];
	q_capacity = atoi(argv[n++]);
	tcp_addr = argv[n++];
	tcp_port = atoi(argv[n]);

	if (FAILED(receiver_create(&recv, mmap_file, q_capacity,
							   0, tcp_addr, tcp_port)) ||
		FAILED(thread_create(&stats_thread, stats_func, recv)))
		error_report_fatal();

	st = receiver_run(recv);

	thread_destroy(&stats_thread);
	putchar('\n');

	if (FAILED(st))
		error_report_fatal();
	
	return 0;
}
