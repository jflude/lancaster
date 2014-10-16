/* generic publisher */

#include "advert.h"
#include "clock.h"
#include "error.h"
#include "sender.h"
#include "signals.h"
#include "sock.h"
#include "thread.h"
#include "udp.h"
#include "version.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define DISPLAY_DELAY_USEC (1 * 1000000)
#define DEFAULT_TTL 1

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

static void *stats_func(thread_handle thr)
{
	sender_handle sender = thread_get_param(thr);
	storage_handle store = sender_get_storage(sender);
	char hostname[256];
	microsec last_print;
	const char *eol_seq;
	status st;
    struct udp_conn_info udp_stat_conn;
    const char *udp_stat_url = getenv("UDP_STATS_URL");
    
    if (udp_stat_url &&
		FAILED(st = open_udp_sock_conn(&udp_stat_conn, udp_stat_url))) {
		sender_stop(sender);
		return (void *)(long)st;
    }
    
	if (FAILED(st = sock_get_hostname(hostname, sizeof(hostname))) ||
		FAILED(st = clock_time(&last_print))) {
		sender_stop(sender);
		return (void *)(long)st;
	}

	eol_seq = (isatty(STDOUT_FILENO) ? "\033[K\r" : "\n");

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

		if (stg_stats) {
			printf("\"%.20s\", STG.REC/s: %.2f, STG.MIN/us: %.2f, "
				   "STG.AVG/us: %.2f, STG.MAX/us: %.2f, STG.STD/us: %.2f%s",
				   storage_get_description(store),
				   sender_get_storage_record_count(sender) / secs,
				   sender_get_storage_min_latency(sender),
				   sender_get_storage_mean_latency(sender),
				   sender_get_storage_max_latency(sender),
				   sender_get_storage_stddev_latency(sender),
				   eol_seq);
		} else {
			if (as_json) {
				int stats_buff_used;
				char ts[64], stats_buf[1024];
				if (FAILED(st = clock_get_text(now, 3, ts, sizeof(ts))))
					break;

				stats_buff_used =
					sprintf(stats_buf,
							"{\"@timestamp\":\"%s\", "
                            "\"host\":\"%s\", "
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
							storage_get_file(store),
							sender_get_receiver_count(sender),
							sender_get_mcast_packets_sent(sender) / secs,
							sender_get_tcp_gap_count(sender) / secs,
							sender_get_tcp_bytes_sent(sender) / secs / 1024,
							sender_get_mcast_bytes_sent(sender) / secs / 1024,
							sender_get_storage_record_count(sender) / secs,
							sender_get_storage_min_latency(sender),
							sender_get_storage_mean_latency(sender),
							sender_get_storage_max_latency(sender),
							sender_get_storage_stddev_latency(sender));

				if (stats_buff_used < 0) {
					st = error_errno("sprintf");
					break;
				}

                if (udp_stat_url) {
					if (FAILED(st = sock_sendto(udp_stat_conn.sock_fd_,
												udp_stat_conn.server_sock_addr_,
												stats_buf, stats_buff_used)))
                        break;
				} else
                    puts(stats_buf);
			} else {
				if (printf("\"%.20s\", RECV: %ld, PKT/s: %.2f, GAP/s: %.2f, "
						   "TCP KB/s: %.2f, MCAST KB/s: %.2f%s",
						   storage_get_description(store),
						   sender_get_receiver_count(sender),
						   sender_get_mcast_packets_sent(sender) / secs,
						   sender_get_tcp_gap_count(sender) / secs,
						   sender_get_tcp_bytes_sent(sender) / secs / 1024,
						   sender_get_mcast_bytes_sent(sender) / secs / 1024,
						   eol_seq) < 0) {
					st = error_errno("printf");
					break;
				}
			}
		}

		if (FAILED(st = sender_roll_stats(sender)))
			break;

		last_print = now;
        if (!udp_stat_url)
			fflush(stdout);
	}

    if (udp_stat_url) {
		status st2 = close_udp_sock_conn(&udp_stat_conn);
		if (!FAILED(st))
			st = st2;
	} else
		putchar('\n');

	sender_stop(sender);
	return (void *)(long)st;
}

int main(int argc, char *argv[])
{
	advert_handle adv;
	sender_handle sender = NULL;
	thread_handle stats_thread;
	int hb, opt, ttl = DEFAULT_TTL;
	const char *mmap_file, *colon, *mcast_iface = NULL;
	char adv_addr[64], mcast_addr[64], tcp_addr[64];
	int mcast_port, tcp_port, adv_port = 0;
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
			colon = strchr(optarg, ':');
			if (!colon)
				show_syntax();

			strncpy(adv_addr, optarg, colon - optarg);
			adv_addr[colon - optarg] = '\0';

			adv_port = atoi(colon + 1);
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

	colon = strchr(argv[optind], ':');
	if (!colon)
		show_syntax();

	strncpy(tcp_addr, argv[optind], colon - argv[optind]);
	tcp_addr[colon - argv[optind++]] = '\0';

	tcp_port = atoi(colon + 1);

	colon = strchr(argv[optind], ':');
	if (!colon)
		show_syntax();

	strncpy(mcast_addr, argv[optind], colon - argv[optind]);
	mcast_addr[colon - argv[optind++]] = '\0';

	mcast_port = atoi(colon + 1);

	hb = atoi(argv[optind++]);
	max_pkt_age = atoi(argv[optind++]);

	if (FAILED(signal_add_handler(SIGHUP)) ||
		FAILED(signal_add_handler(SIGINT)) ||
		FAILED(signal_add_handler(SIGTERM)) ||
		FAILED(sender_create(&sender, mmap_file, tcp_addr, tcp_port,
							 mcast_addr, mcast_port, mcast_iface,
							 ttl, loopback, hb, max_pkt_age)) ||
		(pub_advert &&
		 (FAILED(advert_create(&adv, adv_addr, adv_port, ttl, loopback, env)) ||
		  FAILED(advert_publish(adv, sender)))))
		error_report_fatal();

	if (!as_json && tcp_port == 0)
		printf("listening on port %d\n", (int)sender_get_listen_port(sender));

	if (FAILED(thread_create(&stats_thread, stats_func, sender)) ||
		FAILED(sender_run(sender)) ||
		FAILED(thread_stop(stats_thread, &stats_result)) ||
		FAILED(thread_destroy(&stats_thread)) ||
		FAILED((status)(long)stats_result) ||
		(pub_advert && FAILED(advert_destroy(&adv))) ||
		FAILED(sender_destroy(&sender)) ||
		FAILED(signal_remove_handler(SIGHUP)) ||
		FAILED(signal_remove_handler(SIGINT)) ||
		FAILED(signal_remove_handler(SIGTERM))) {
		if (!as_json)
			putchar('\n');

		error_report_fatal();
	}

	return 0;
}
