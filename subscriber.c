/* generic subscriber */

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "a2i.h"
#include "error.h"
#include "clock.h"
#include "receiver.h"
#include "reporter.h"
#include "signals.h"
#include "sock.h"
#include "thread.h"
#include "version.h"

#define DISPLAY_DELAY_USEC (1 * 1000000)
#define STATS_ENV_VAR "UDP_STATS_URL"

static receiver_handle rcvr;
static boolean as_json = FALSE;

static void show_syntax(void)
{
	fprintf(stderr, "Syntax: %s [-v] [-j] [-p ERROR PREFIX] STORAGE-FILE "
			"CHANGE-QUEUE-SIZE TCP-ADDRESS:PORT\n", error_get_program_name());

	exit(-SYNTAX_ERROR);
}

static status output_json(double secs, microsec now, const char *hostname,
						  const char *alias, reporter_handle reporter)
{
	status st;
	char ts[64], buf[1024];
	if (FAILED(st = clock_get_text(now, 3, ts, sizeof(ts))))
		return st;

	if (sprintf(buf,
				"{\"@timestamp\":\"%s\", "
				"\"host\":\"%s\", "
				"\"type\":\"subscriber\", "
				"\"app\":\"subscriber\", "
				"\"cat\":\"data_feed\", "
				"\"alias\":\"%s\", "
				"\"storage\":\"%s\", "
				"\"pkt/s\":%.2f, "
				"\"gap/s\":%.2f, "
				"\"tcp_kb/s\":%.2f, "
				"\"mcast_kb/s\":%.2f, "
				"\"min/us\":%.2f, "
				"\"avg/us\":%.2f, "
				"\"max/us\":%.2f, "
				"\"std/us\":%.2f}",
				ts,
				hostname,
				alias,
				storage_get_file(receiver_get_storage(rcvr)),
				receiver_get_mcast_packets_recv(rcvr) / secs,
				receiver_get_tcp_gap_count(rcvr) / secs,
				receiver_get_tcp_bytes_recv(rcvr) / secs / 1024,
				receiver_get_mcast_bytes_recv(rcvr) / secs / 1024,
				receiver_get_mcast_min_latency(rcvr),
				receiver_get_mcast_mean_latency(rcvr),
				receiver_get_mcast_max_latency(rcvr),
				receiver_get_mcast_stddev_latency(rcvr)) < 0)
		return error_errno("sprintf");

	if (reporter) {
		if (FAILED(st = reporter_send(reporter, buf)))
			return st;
	} else if (puts(buf) == EOF)
		return error_errno("puts");

	return OK;
}

static status output_std(double secs)
{
	status st = OK;
	if (printf("\"%.20s\", PKT/s: %.2f, GAP/s: %.2f, "
			   "TCP KB/s: %.2f, MCAST KB/s: %.2f, "
			   "MIN/us: %.2f, AVG/us: %.2f, MAX/us: %.2f, "
			   "STD/us: %.2f%s",
			   storage_get_description(receiver_get_storage(rcvr)),
			   receiver_get_mcast_packets_recv(rcvr) / secs,
			   receiver_get_tcp_gap_count(rcvr) / secs,
			   receiver_get_tcp_bytes_recv(rcvr) / secs / 1024,
			   receiver_get_mcast_bytes_recv(rcvr) / secs / 1024,
			   receiver_get_mcast_min_latency(rcvr),
			   receiver_get_mcast_mean_latency(rcvr),
			   receiver_get_mcast_max_latency(rcvr),
			   receiver_get_mcast_stddev_latency(rcvr),
			   (isatty(STDOUT_FILENO) ? "\033[K\r" : "\n")) < 0)
		st = error_errno("printf");

	return st;
}

static void show_version(void)
{
	printf("subscriber %s\n", version_get_source());
	exit(0);
}

static void *stats_func(thread_handle thr)
{
	reporter_handle reporter = NULL;
	char hostname[256], alias[32], udp_address[64];
	unsigned short udp_port;
	const char *storage_desc =
		storage_get_description(receiver_get_storage(rcvr));
	microsec last_print;
	status st;

    if (as_json) {
		const char *delim_pos;
		if (FAILED(st = sock_get_hostname(hostname, sizeof(hostname))) ||
			(getenv(STATS_ENV_VAR) &&
			 (FAILED(st = sock_addr_split(getenv(STATS_ENV_VAR), udp_address,
										  sizeof(udp_address), &udp_port)) ||
			  FAILED(st = reporter_create(&reporter, udp_address, udp_port))))) {
			receiver_stop(rcvr);
			return (void *)(long)st;
		}

		if (!(delim_pos = strchr(storage_desc, '.')))
			strncpy(alias, "unknown", sizeof(alias));
		else
			strncpy(alias, storage_desc, delim_pos - storage_desc);
	}

	if (FAILED(st = clock_time(&last_print))) {
		reporter_destroy(&reporter);
		receiver_stop(rcvr);
		return (void *)(long)st;
	}

	while (!thread_is_stopping(thr)) {
		microsec now;
		double secs;
		if (FAILED(st = signal_is_raised(SIGHUP)) ||
			FAILED(st = signal_is_raised(SIGINT)) ||
			FAILED(st = signal_is_raised(SIGTERM)) ||
			FAILED(st = clock_sleep(DISPLAY_DELAY_USEC)) ||
			FAILED(st = clock_time(&now)))
			break;

		secs = (now - last_print) / 1000000.0;

		if (as_json)
			output_json(secs, now, hostname, alias, reporter);
		else
			output_std(secs);

		if (FAILED(st = receiver_roll_stats(rcvr)))
			break;

		last_print = now;
		if (!reporter)
			fflush(stdout);
	}

    if (reporter) {
		status st2 = reporter_destroy(&reporter);
		if (!FAILED(st))
			st = st2;
	} else
		putchar('\n');

	receiver_stop(rcvr);
	return (void *)(long)st;
}

int main(int argc, char *argv[])
{
	thread_handle stats_thread;
	const char *mmap_file;
	char tcp_addr[64];
	unsigned short tcp_port;
	long q_capacity;
	int opt;
	void *stats_result;

	char prog_name[256];
	strcpy(prog_name, argv[0]);
	error_set_program_name(prog_name);

	while ((opt = getopt(argc, argv, "jp:v")) != -1)
		switch (opt) {
		case 'j':
			as_json = TRUE;
			break;
		case 'p':
			strcat(prog_name, ": ");
			strcat(prog_name, optarg);
			error_set_program_name(prog_name);
			break;
		case 'v':
			show_version();
		default:
			show_syntax();
		}

	if ((argc - optind) != 3)
		show_syntax();

	mmap_file = argv[optind++];

	if (FAILED(a2i(argv[optind++], "%ld", &q_capacity)))
		error_report_fatal();

	if (q_capacity <= 1 || (q_capacity & (q_capacity - 1)) != 0) {
		error_invalid_arg("change queue size not a non-zero power of 2");
		error_report_fatal();
	}

	if (FAILED(sock_addr_split(argv[optind++], tcp_addr,
							   sizeof(tcp_addr), &tcp_port)) ||
		FAILED(signal_add_handler(SIGHUP)) ||
		FAILED(signal_add_handler(SIGINT)) ||
		FAILED(signal_add_handler(SIGTERM)) ||
		FAILED(receiver_create(&rcvr, mmap_file, q_capacity,
							   0, tcp_addr, tcp_port)) ||
		FAILED(thread_create(&stats_thread, stats_func, NULL)) ||
		FAILED(receiver_run(rcvr)) ||
		FAILED(thread_stop(stats_thread, &stats_result)) ||
		FAILED(thread_destroy(&stats_thread)) ||
		FAILED((status)(long)stats_result) ||
		FAILED(receiver_destroy(&rcvr)) ||
		FAILED(signal_remove_handler(SIGHUP)) ||
		FAILED(signal_remove_handler(SIGINT)) ||
		FAILED(signal_remove_handler(SIGTERM))) {
		if (!as_json)
			putchar('\n');
	
		error_report_fatal();
	}

	return 0;
}
