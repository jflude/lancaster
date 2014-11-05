/* generic publisher */

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "advert.h"
#include "clock.h"
#include "error.h"
#include "reporter.h"
#include "sender.h"
#include "signals.h"
#include "sock.h"
#include "thread.h"
#include "version.h"

#define DISPLAY_DELAY_USEC (1 * 1000000)
#define DEFAULT_TTL 1
#define STATS_ENV_VAR "UDP_STATS_URL"

static sender_handle sndr;
static boolean as_json;
static boolean stg_stats;

static void show_syntax(void)
{
	fprintf(stderr, "Syntax: %s [-v] [-a ADDRESS:PORT] [-e ENV] [-i DEVICE] "
			"[-j|-s] [-l] [-p ERROR PREFIX] [-t TTL] STORAGE-FILE "
			"TCP-ADDRESS:PORT MULTICAST-ADDRESS:PORT HEARTBEAT-PERIOD "
			"MAXIMUM-PACKET-AGE\n",
			error_get_program_name());

	exit(-SYNTAX_ERROR);
}

static void show_version(void)
{
	printf("publisher %s\n", version_get_source());
	exit(0);
}

static status output_stg(double secs)
{
	if (printf("\"%.20s\", STG.REC/s: %.2f, STG.MIN/us: %.2f, "
			   "STG.AVG/us: %.2f, STG.MAX/us: %.2f, STG.STD/us: %.2f%s",
			   storage_get_description(sender_get_storage(sndr)),
			   sender_get_storage_record_count(sndr) / secs,
			   sender_get_storage_min_latency(sndr),
			   sender_get_storage_mean_latency(sndr),
			   sender_get_storage_max_latency(sndr),
			   sender_get_storage_stddev_latency(sndr),
			   (isatty(STDOUT_FILENO) ? "\033[K\r" : "\n")) < 0)
		return error_errno("printf");

	return OK;
}

static status output_json(double secs, microsec now, const char *hostname,
						  reporter_handle reporter)
{
	status st;
	char ts[64], buf[1024];
	if (FAILED(st = clock_get_text(now, 3, ts, sizeof(ts))))
		return st;

	if (sprintf(buf,
				"{\"@timestamp\":\"%s\", "
				"\"host\":\"%s\", "
				"\"type\":\"publisher\", "
				"\"app\":\"publisher\", "
				"\"cat\":\"data_feed\", "
				"\"storage\":\"%s\", "
				"\"recv\":%ld, "
				"\"pkt/s\":%.2f, "
				"\"gap/s\":%.2f, "
				"\"tcp_kb/s\":%.2f, "
				"\"mcast_kb/s\":%.2f, "
				"\"stg_rec/s\":%.2f, "
				"\"stg_min/us\":%.2f, "
				"\"stg_avg/us\":%.2f, "
				"\"stg_max/us\":%.2f, "
				"\"stg_std/us\":%.2f}",
				ts,
				hostname,
				storage_get_file(sender_get_storage(sndr)),
				sender_get_receiver_count(sndr),
				sender_get_mcast_packets_sent(sndr) / secs,
				sender_get_tcp_gap_count(sndr) / secs,
				sender_get_tcp_bytes_sent(sndr) / secs / 1024,
				sender_get_mcast_bytes_sent(sndr) / secs / 1024,
				sender_get_storage_record_count(sndr) / secs,
				sender_get_storage_min_latency(sndr),
				sender_get_storage_mean_latency(sndr),
				sender_get_storage_max_latency(sndr),
				sender_get_storage_stddev_latency(sndr)) < 0)
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
	if (printf("\"%.20s\", RECV: %ld, PKT/s: %.2f, GAP/s: %.2f, "
			   "TCP KB/s: %.2f, MCAST KB/s: %.2f%s",
			   storage_get_description(sender_get_storage(sndr)),
			   sender_get_receiver_count(sndr),
			   sender_get_mcast_packets_sent(sndr) / secs,
			   sender_get_tcp_gap_count(sndr) / secs,
			   sender_get_tcp_bytes_sent(sndr) / secs / 1024,
			   sender_get_mcast_bytes_sent(sndr) / secs / 1024,
			   (isatty(STDOUT_FILENO) ? "\033[K\r" : "\n")) < 0)
		return error_errno("printf");

	return OK;
}

static void *stats_func(thread_handle thr)
{
	reporter_handle reporter = NULL;
	char hostname[256], udp_address[64];
	unsigned short udp_port;
	microsec last_print;
	status st;
    
    if (as_json) {
		if (FAILED(st = sock_get_hostname(hostname, sizeof(hostname))) ||
			(getenv(STATS_ENV_VAR) &&
			 (FAILED(st = sock_addr_split(getenv(STATS_ENV_VAR), udp_address,
										  sizeof(udp_address), &udp_port)) ||
			  FAILED(st = reporter_create(&reporter, udp_address, udp_port))))) {
			sender_stop(sndr);
			return (void *)(long)st;
		}
	}
    
	if (FAILED(st = clock_time(&last_print))) {
		reporter_destroy(&reporter);
		sender_stop(sndr);
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

		if (stg_stats)
			output_stg(secs);
		else if (as_json)
			output_json(secs, now, hostname, reporter);
		else
			output_std(secs);

		if (FAILED(st = sender_roll_stats(sndr)))
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

	sender_stop(sndr);
	return (void *)(long)st;
}

int main(int argc, char *argv[])
{
	advert_handle adv;
	thread_handle stats_thread;
	int hb, opt, ttl = DEFAULT_TTL;
	const char *mmap_file, *mcast_iface = NULL;
	char adv_addr[64], mcast_addr[64], tcp_addr[64];
	unsigned short mcast_port, tcp_port, adv_port = 0;
	boolean pub_advert = FALSE, loopback = FALSE;
	microsec max_pkt_age;
	void *stats_result;
	char *env = "";

	char prog_name[256];
	strcpy(prog_name, argv[0]);
	error_set_program_name(prog_name);

	while ((opt = getopt(argc, argv, "a:e:i:jlp:st:v")) != -1)
		switch (opt) {
		case 'a':
			if (FAILED(sock_addr_split(optarg, adv_addr,
									   sizeof(adv_addr), &adv_port)))
				error_report_fatal();

			pub_advert = TRUE;
			break;
		case 'e':
			env = optarg;
			break;
		case 'i':
			mcast_iface = optarg;
			break;
		case 'j':
			if (stg_stats)
				show_syntax();

			as_json = TRUE;
			break;
		case 'l':
			loopback = TRUE;
			break;
		case 'p':
			strcat(prog_name, ": ");
			strcat(prog_name, optarg);
			error_set_program_name(prog_name);
			break;
		case 's':
			if (as_json)
				show_syntax();

			stg_stats = TRUE;
			break;
		case 't':
			ttl = atoi(optarg);
			break;
		case 'v':
			show_version();
		default:
			show_syntax();
		}

	if ((argc - optind) != 5)
		show_syntax();

	mmap_file = argv[optind++];

	if (FAILED(sock_addr_split(argv[optind++], tcp_addr,
							   sizeof(tcp_addr), &tcp_port)) ||
		FAILED(sock_addr_split(argv[optind++], mcast_addr,
							   sizeof(mcast_addr), &mcast_port)))
		error_report_fatal();

	hb = atoi(argv[optind++]);
	max_pkt_age = atoi(argv[optind++]);

	if (FAILED(signal_add_handler(SIGHUP)) ||
		FAILED(signal_add_handler(SIGINT)) ||
		FAILED(signal_add_handler(SIGTERM)) ||
		FAILED(sender_create(&sndr, mmap_file, tcp_addr, tcp_port,
							 mcast_addr, mcast_port, mcast_iface,
							 ttl, loopback, hb, max_pkt_age)) ||
		(pub_advert &&
		 (FAILED(advert_create(&adv, adv_addr, adv_port, ttl, loopback, env)) ||
		  FAILED(advert_publish(adv, sndr)))))
		error_report_fatal();

	if (!as_json && tcp_port == 0)
		printf("listening on port %d\n", (int)sender_get_listen_port(sndr));

	if (FAILED(thread_create(&stats_thread, stats_func, NULL)) ||
		FAILED(sender_run(sndr)) ||
		FAILED(thread_stop(stats_thread, &stats_result)) ||
		FAILED(thread_destroy(&stats_thread)) ||
		FAILED((status)(long)stats_result) ||
		(pub_advert && FAILED(advert_destroy(&adv))) ||
		FAILED(sender_destroy(&sndr)) ||
		FAILED(signal_remove_handler(SIGHUP)) ||
		FAILED(signal_remove_handler(SIGINT)) ||
		FAILED(signal_remove_handler(SIGTERM))) {
		if (!as_json)
			putchar('\n');

		error_report_fatal();
	}

	return 0;
}
