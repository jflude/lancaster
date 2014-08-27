/* generic publisher */

#include "advert.h"
#include "clock.h"
#include "error.h"
#include "sender.h"
#include "sock.h"
#include "thread.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DISPLAY_DELAY_USEC 1000000

#define ADVERT_ADDRESS "227.1.1.227"
#define ADVERT_PORT 11227
#define DEFAULT_TTL 64

static void syntax(const char* prog)
{
	fprintf(stderr, "Syntax: %s [-v|--verbose] [heartbeat interval] [maximum packet age] [multicast address] "
			"[multicast port] [TCP address] [TCP port] [storage file or segment]\n", prog);
	exit(EXIT_FAILURE);
}

static void* stats_func(thread_handle thr)
{
	sender_handle sender = thread_get_param(thr);
	microsec last_print;
	status st;

	size_t pkt1 = sender_get_mcast_packets_sent(sender);
	size_t tcp1 = sender_get_tcp_bytes_sent(sender);
	size_t mcast1 = sender_get_mcast_bytes_sent(sender);

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
		pkt2 = sender_get_mcast_packets_sent(sender);
		tcp2 = sender_get_tcp_bytes_sent(sender);
		mcast2 = sender_get_mcast_bytes_sent(sender);

		printf("\"%.16s\", RECV: %ld, PKT/sec: %.2f, GAP: %lu, TCP KB/sec: %.2f, MCAST KB/sec: %.2f           \r",
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

	putchar('\n');
	return (void*) (long) st;
}

int main(int argc, char* argv[])
{
	advert_handle adv;
	sender_handle sender;
	thread_handle stats_thread;
	int hb, n = 1;
	const char *mcast_addr, *tcp_addr;
	int mcast_port, tcp_port;
	microsec max_pkt_age;
	boolean verbose = FALSE;

	if (argc < 8 || argc > 9)
		syntax(argv[0]);

	if (strcmp(argv[n], "-v") == 0 || strcmp(argv[n], "--verbose") == 0) {
		if (argc != 9)
			syntax(argv[0]);

		verbose = TRUE;
		++n;
	}

	hb = atoi(argv[n++]);
	max_pkt_age = atoi(argv[n++]);
	mcast_addr = argv[n++];
	mcast_port = atoi(argv[n++]);
	tcp_addr = argv[n++];
	tcp_port = atoi(argv[n++]);

	if (FAILED(sender_create(&sender, argv[n], hb, max_pkt_age, mcast_addr, mcast_port, DEFAULT_TTL, tcp_addr, tcp_port)) ||
		FAILED(advert_create(&adv, ADVERT_ADDRESS, ADVERT_PORT, DEFAULT_TTL)) ||
		FAILED(advert_publish(adv, sender)))
		error_report_fatal();

	if (tcp_port == 0)
		printf("listening on port %d\n", sender_get_listen_port(sender));

	if (verbose && FAILED(thread_create(&stats_thread, stats_func, sender)))
		error_report_fatal();

	if (FAILED(sender_run(sender))) {
		if (verbose)
			putchar('\n');

		error_report_fatal();
	}

	return 0;
}
