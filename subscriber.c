/* generic subscriber */

#include "error.h"
#include "clock.h"
#include "receiver.h"
#include "signals.h"
#include "sock.h"
#include "thread.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define DISPLAY_DELAY_USEC (1 * 1000000)

boolean as_json = FALSE;

static void show_syntax(void)
{
	fprintf(stderr, "Syntax: %s [-v] [-j] STORAGE-FILE CHANGE-QUEUE-SIZE "
			"TCP-ADDRESS:PORT\n", error_get_program_name());

	exit(1);
}

static void show_version(void)
{
	printf("subscriber 1.0\n");
	exit(0);
}

static void* stats_func(thread_handle thr)
{
	receiver_handle recv = thread_get_param(thr);
	char hostname[256];
	microsec last_print;
	status st;
	char alias[32];
	const char *storage_desc, *delim_pos;
	
	size_t pkt1 = receiver_get_mcast_packets_recv(recv);
	size_t tcp1 = receiver_get_tcp_bytes_recv(recv);
	size_t mcast1 = receiver_get_mcast_bytes_recv(recv);
	
	storage_desc = storage_get_description(receiver_get_storage(recv));

	if ((delim_pos = strchr(storage_desc, '.')) == NULL)
		strncpy(alias, "unknown", sizeof(alias));
	else
		strncpy(alias, storage_desc, delim_pos - storage_desc);
	
	if (FAILED(st = sock_get_hostname(hostname, sizeof(hostname))) ||
		FAILED(st = clock_time(&last_print)))
		return (void*) (long) st;

	while (!thread_is_stopping(thr)) {
		double secs;
		size_t pkt2, tcp2, mcast2;
		microsec now, elapsed;

		if (FAILED(st = signal_is_raised(SIGHUP)) ||
			FAILED(st = signal_is_raised(SIGINT)) ||
			FAILED(st = signal_is_raised(SIGTERM)) ||
			FAILED(st = clock_sleep(DISPLAY_DELAY_USEC)) ||
			FAILED(st = clock_time(&now)))
			break;

		elapsed = now - last_print;
		secs = elapsed / 1000000.0;
		pkt2 = receiver_get_mcast_packets_recv(recv);
		tcp2 = receiver_get_tcp_bytes_recv(recv);
		mcast2 = receiver_get_mcast_bytes_recv(recv);

		if (as_json) {
			char ts[64];
			if (FAILED(st = clock_get_text(now, ts, sizeof(ts))))
				break;

			printf("{ \"@timestamp\":\"%s\", "
				   "\"app\":\"subscriber\", "
				   "\"cat\":\"data_feed\", "
				   "\"host\":\"%s\", "
				   "\"alias\":\"%s\", "
				   "\"storage\":\"%.20s\", "
				   "\"pkt/s\":%.2f, "
				   "\"gap\":%lu, "
				   "\"tcp_kb/s\":%.2f, "
				   "\"mcast_kb/s\":%.2f, "
				   "\"min/us\":%.2f, "
				   "\"avg/us\":%.2f, "
				   "\"max/us\":%.2f, "
				   "\"std/us\":%.2f }\n",
				   ts,
				   hostname,
				   alias,
				   storage_get_file(receiver_get_storage(recv)),
				   (pkt2 - pkt1) / secs,
				   receiver_get_tcp_gap_count(recv),
				   (tcp2 - tcp1) / secs / 1024,
				   (mcast2 - mcast1) / secs / 1024,
				   receiver_get_mcast_min_latency(recv),
				   receiver_get_mcast_mean_latency(recv),
				   receiver_get_mcast_max_latency(recv),
				   receiver_get_mcast_stddev_latency(recv));
		} else {
			printf("\"%.20s\", PKT/s: %.2f, GAP: %lu, "
				   "TCP KB/s: %.2f, MCAST KB/s: %.2f, "
				   "MIN/us: %.2f, AVG/us: %.2f, MAX/us: %.2f, "
				   "STD/us: %.2f         \r",
				   storage_desc,
				   (pkt2 - pkt1) / secs,
				   receiver_get_tcp_gap_count(recv),
				   (tcp2 - tcp1) / secs / 1024,
				   (mcast2 - mcast1) / secs / 1024,
				   receiver_get_mcast_min_latency(recv),
				   receiver_get_mcast_mean_latency(recv),
				   receiver_get_mcast_max_latency(recv),
				   receiver_get_mcast_stddev_latency(recv));
		}

		fflush(stdout);

		last_print = now;
		pkt1 = pkt2;
		tcp1 = tcp2;
		mcast1 = mcast2;
	}

	receiver_stop(recv);

	putchar('\n');
	return (void*) (long) st;
}

int main(int argc, char* argv[])
{
	receiver_handle recv;
	thread_handle stats_thread;
	const char *mmap_file, *tcp_addr;
	char* colon;
	int tcp_port;
	size_t q_capacity;
	int opt;
	void* stats_result;

	error_set_program_name(argv[0]);

	while ((opt = getopt(argc, argv, "jv")) != -1)
		switch (opt) {
		case 'j':
			as_json = TRUE;
			break;
		case 'v':
			show_version();
		default:
			show_syntax();
		}

	if ((argc - optind) != 3)
		show_syntax();

	mmap_file = argv[optind++];
	q_capacity = atoi(argv[optind++]);

	tcp_addr = argv[optind++];
	colon = strchr(tcp_addr, ':');
	if (!colon)
		show_syntax();

	*colon = '\0';
	tcp_port = atoi(colon + 1);

	if (FAILED(signal_add_handler(SIGHUP)) ||
		FAILED(signal_add_handler(SIGINT)) ||
		FAILED(signal_add_handler(SIGTERM)) ||
		FAILED(receiver_create(&recv, mmap_file, q_capacity,
							   0, tcp_addr, tcp_port)) ||
		FAILED(thread_create(&stats_thread, stats_func, recv)) ||
		FAILED(receiver_run(recv)) ||
		FAILED(thread_stop(stats_thread, &stats_result)) ||
		FAILED(thread_destroy(&stats_thread)) ||
		FAILED((status) (long) stats_result) ||
		FAILED(receiver_destroy(&recv)) ||
		FAILED(signal_remove_handler(SIGHUP)) ||
		FAILED(signal_remove_handler(SIGINT)) ||
		FAILED(signal_remove_handler(SIGTERM))) {
		if (!as_json)
			putchar('\n');
	
		error_report_fatal();
	}

	return 0;
}
