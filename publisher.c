/* generic publisher */

#include "advert.h"
#include "clock.h"
#include "error.h"
#include "sender.h"
#include "signals.h"
#include "sock.h"
#include "thread.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define DISPLAY_DELAY_USEC (1 * 1000000)
#define DEFAULT_TTL 1

static boolean as_json;

static void show_syntax(void)
{
	fprintf(stderr, "Syntax: %s [-v] [-j] [-a ADDRESS:PORT] [-i DEVICE] [-l] "
			"[-t TTL] [-e ENV] STORAGE-FILE TCP-ADDRESS:PORT MULTICAST-ADDRESS:PORT "
			"HEARTBEAT-PERIOD MAXIMUM-PACKET-AGE\n",
			error_get_program_name());

	exit(SYNTAX_ERROR);
}

static void show_version(void)
{
	printf("publisher 1.0\n");
	exit(0);
}

static void* stats_func(thread_handle thr)
{
	sender_handle sender = thread_get_param(thr);
	char hostname[256];
	microsec last_print;
	status st;

	size_t pkt1 = sender_get_mcast_packets_sent(sender);
	size_t tcp1 = sender_get_tcp_bytes_sent(sender);
	size_t mcast1 = sender_get_mcast_bytes_sent(sender);

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
		pkt2 = sender_get_mcast_packets_sent(sender);
		tcp2 = sender_get_tcp_bytes_sent(sender);
		mcast2 = sender_get_mcast_bytes_sent(sender);

		if (as_json) {
			char ts[64];
			if (FAILED(st = clock_get_text(now, ts, sizeof(ts))))
				break;

			printf("{\"@timestamp\":\"%s\", "
				   "\"app\":\"publisher\", "
				   "\"cat\":\"data_feed\", "
				   "\"storage\":\"%s\", "
				   "\"recv\":%ld, "
				   "\"pkt/s\":%.2f, "
				   "\"gap\":%lu, "
				   "\"tcp/s\":%.2f, "
				   "\"mcast/s\":%.2f}\n",
				   ts,
				   storage_get_file(sender_get_storage(sender)),
				   sender_get_receiver_count(sender),
				   (pkt2 - pkt1) / secs,
				   sender_get_tcp_gap_count(sender),
				   (tcp2 - tcp1) / secs / 1024,
				   (mcast2 - mcast1) / secs / 1024);
		} else
			printf("\"%.20s\", RECV: %ld, PKT/s: %.2f, GAP: %lu, "
				   "TCP KB/s: %.2f, MCAST KB/s: %.2f         \r",
				   storage_get_description(sender_get_storage(sender)),
				   sender_get_receiver_count(sender),
				   (pkt2 - pkt1) / secs,
				   sender_get_tcp_gap_count(sender),
				   (tcp2 - tcp1) / secs / 1024,
				   (mcast2 - mcast1) / secs / 1024);

		fflush(stdout);
		last_print = now;
		pkt1 = pkt2;
		tcp1 = tcp2;
		mcast1 = mcast2;
	}

	sender_stop(sender);

	putchar('\n');
	return (void*) (long) st;
}

int main(int argc, char* argv[])
{
	advert_handle adv;
	sender_handle sender;
	thread_handle stats_thread;
	int hb, opt, ttl = DEFAULT_TTL;
	const char *mmap_file, *mcast_addr, *tcp_addr;
	const char *mcast_iface = NULL, *adv_addr = NULL;
	char* colon;
	int mcast_port, tcp_port, adv_port = 0;
	boolean loopback = FALSE;
	microsec max_pkt_age;
	void* stats_result;
	char* env = "";

	error_set_program_name(argv[0]);

	while ((opt = getopt(argc, argv, "a:ji:lve:")) != -1)
		switch (opt) {
		case 'a':
			adv_addr = optarg;
			colon = strchr(adv_addr, ':');
			if (!colon)
				show_syntax();

			*colon = '\0';
			adv_port = atoi(colon + 1);
			break;
		case 'j':
			as_json = TRUE;
			break;
		case 'i':
			mcast_iface = optarg;
			break;
		case 'l':
			loopback = TRUE;
			break;
		case 't':
			ttl = atoi(optarg);
			break;
		case 'e':
			env = optarg;
			break;
		case 'v':
			show_version();
		default:
			show_syntax();
		}

	if ((argc - optind) != 5)
		show_syntax();

	mmap_file = argv[optind++];

	tcp_addr = argv[optind++];
	colon = strchr(tcp_addr, ':');
	if (!colon)
		show_syntax();

	*colon = '\0';
	tcp_port = atoi(colon + 1);

	mcast_addr = argv[optind++];
	colon = strchr(mcast_addr, ':');
	if (!colon)
		show_syntax();

	*colon = '\0';
	mcast_port = atoi(colon + 1);

	hb = atoi(argv[optind++]);
	max_pkt_age = atoi(argv[optind++]);

	if (FAILED(signal_add_handler(SIGHUP)) ||
		FAILED(signal_add_handler(SIGINT)) ||
		FAILED(signal_add_handler(SIGTERM)) ||
		FAILED(sender_create(&sender, mmap_file, tcp_addr, tcp_port,
							 mcast_addr, mcast_port, mcast_iface,
							 ttl, loopback, hb, max_pkt_age)) ||
		(adv_addr &&
		 (FAILED(advert_create(&adv, adv_addr, adv_port, ttl, loopback, env)) ||
		  FAILED(advert_publish(adv, sender)))))
		error_report_fatal();

	if (!as_json && tcp_port == 0)
		printf("listening on port %d\n", (int) sender_get_listen_port(sender));

	if (FAILED(thread_create(&stats_thread, stats_func, sender)) ||
		FAILED(sender_run(sender)) ||
		FAILED(thread_stop(stats_thread, &stats_result)) ||
		FAILED(thread_destroy(&stats_thread)) ||
		FAILED((status) (long) stats_result) ||
		(adv_addr && FAILED(advert_destroy(&adv))) ||
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
