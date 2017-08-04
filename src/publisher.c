/*
   Copyright (c)2014-2017 Peak6 Investments, LP.  All rights reserved.
   Use of this source code is governed by the LICENSE file.
*/

/* generic publisher */

#include <lancaster/a2i.h>
#include <lancaster/advert.h>
#include <lancaster/clock.h>
#include <lancaster/error.h>
#include <lancaster/reporter.h>
#include <lancaster/sender.h>
#include <lancaster/signals.h>
#include <lancaster/socket.h>
#include <lancaster/thread.h>
#include <lancaster/version.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define DEFAULT_ADVERT_USEC (10 * 1000000)
#define DEFAULT_HEARTBEAT_USEC (1 * 1000000)
#define DEFAULT_MAX_PKT_AGE_USEC (2 * 1000)
#define DEFAULT_MCAST_TTL 1
#define DEFAULT_ORPHAN_USEC (3 * 1000000)
#define DISPLAY_DELAY_USEC (1 * 1000000)

static sender_handle sndr;
static reporter_handle reporter;
static char hostname[256];
static boolean as_json, stg_stats;

static void show_syntax(void)
{
    fprintf(stderr, "Syntax: %s [-v] [-a ADVERT-ADDRESS:PORT] "
	    "[-A ADVERT-PERIOD] [-e ENVIRONMENT] [-H HEARTBEAT-PERIOD] "
	    "[-i DATA-INTERFACE] [-I ADVERT-INTERFACE] [-j|-s] [-l] [-L] "
	    "[-O ORPHAN-TIMEOUT] [-p ERROR PREFIX] [-P MAXIMUM-PACKET-AGE] "
	    "[-Q] [-R] [-S STATISTICS-UDP-ADDRESS:PORT] [-t TTL] STORAGE-FILE "
	    "TCP-ADDRESS:PORT MULTICAST-ADDRESS:PORT\n",
	    error_get_program_name());

    exit(-SYNTAX_ERROR);
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

static status output_json(double secs, microsec now)
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
    microsec last_print;
    status st;

    if (FAILED(st = clock_time(&last_print))) {
	sender_stop(sndr);
	return (void *)(long)st;
    }

    while (!thread_is_stopping(thr)) {
	microsec now;
	double secs;
	if (FAILED(st = signal_any_raised()) ||
	    FAILED(st = clock_sleep(DISPLAY_DELAY_USEC)) ||
	    FAILED(st = clock_time(&now)))
	    break;

	secs = (now - last_print) / 1000000.0;

	if (stg_stats)
	    output_stg(secs);
	else if (as_json)
	    output_json(secs, now);
	else
	    output_std(secs);

	if (FAILED(st = sender_roll_stats(sndr)))
	    break;

	last_print = now;

	if (!reporter && fflush(stdout) == -1) {
	    st = error_errno("fflush");
	    break;
	}
    }

    if (!reporter)
	putchar('\n');

    sender_stop(sndr);
    return (void *)(long)st;
}

int main(int argc, char *argv[])
{
    advert_handle adv = NULL;
    thread_handle stats_thread;
    const char *mmap_file, *mcast_iface = NULL, *adv_iface = NULL;
    char mcast_addr[64], tcp_addr[64], stats_addr[64], adv_addr[64];
    unsigned short mcast_port, tcp_port, stats_port, adv_port = 0;
    boolean pub_advert = FALSE, loopback = FALSE,
	ignore_recreate = FALSE, ignore_overrun = FALSE;
    microsec hb_period = DEFAULT_HEARTBEAT_USEC,
	orphan_timeout = DEFAULT_ORPHAN_USEC,
	adv_period = DEFAULT_ADVERT_USEC,
	max_pkt_age = DEFAULT_MAX_PKT_AGE_USEC;
    short mcast_ttl = DEFAULT_MCAST_TTL;
    void *stats_result;
    char *env = "";
    int opt;

    char prog_name[256];
    strcpy(prog_name, argv[0]);
    error_set_program_name(prog_name);

    while ((opt = getopt(argc, argv, "a:A:e:H:i:I:jlLO:p:P:QRsS:t:v")) != -1)
	switch (opt) {
	case 'a':
	    if (FAILED(sock_addr_split(optarg, adv_addr,
				       sizeof(adv_addr), &adv_port)))
		error_report_fatal();

	    pub_advert = TRUE;
	    break;
	case 'A':
	    if (FAILED(a2i(optarg, "%ld", &adv_period)))
		error_report_fatal();
	    break;
	case 'e':
	    env = optarg;
	    break;
	case 'H':
	    if (FAILED(a2i(optarg, "%ld", &hb_period)))
		error_report_fatal();
	    break;
	case 'i':
	    mcast_iface = optarg;
	    break;
	case 'I':
	    adv_iface = optarg;
	    break;
	case 'j':
	    if (stg_stats)
		show_syntax();

	    if (FAILED(sock_get_hostname(hostname, sizeof(hostname))))
		error_report_fatal();

	    as_json = TRUE;
	    break;
	case 'l':
	    loopback = TRUE;
	    break;
	case 'L':
	    error_with_timestamp(TRUE);
	    break;
	case 'O':
	    if (FAILED(a2i(optarg, "%ld", &orphan_timeout)))
		error_report_fatal();
	    break;
	case 'p':
	    strcat(prog_name, ": ");
	    strcat(prog_name, optarg);
	    error_set_program_name(prog_name);
	    break;
	case 'P':
	    if (FAILED(a2i(optarg, "%ld", &max_pkt_age)))
		error_report_fatal();
	    break;
	case 'Q':
	    ignore_overrun = TRUE;
	    break;
	case 'R':
	    ignore_recreate = TRUE;
	    break;
	case 's':
	    if (as_json)
		show_syntax();

	    stg_stats = TRUE;
	    break;
	case 'S':
	    if (FAILED(sock_addr_split(optarg, stats_addr,
				       sizeof(stats_addr), &stats_port)) ||
		FAILED(reporter_create(&reporter, stats_addr, stats_port)))
		error_report_fatal();
	    break;
	case 't':
	    if (FAILED(a2i(optarg, "%hd", &mcast_ttl)))
		error_report_fatal();
	    break;
	case 'v':
	    show_version("publisher");
	default:
	    show_syntax();
	}

    if ((argc - optind) != 3)
	show_syntax();

    mmap_file = argv[optind++];

    if (FAILED(sock_addr_split(argv[optind++], tcp_addr,
			       sizeof(tcp_addr), &tcp_port)) ||
	FAILED(sock_addr_split(argv[optind++], mcast_addr,
			       sizeof(mcast_addr), &mcast_port)) ||
	FAILED(signal_add_handler(SIGHUP)) ||
	FAILED(signal_add_handler(SIGINT)) ||
	FAILED(signal_add_handler(SIGTERM)) ||
	FAILED(sender_create(&sndr, mmap_file, tcp_addr, tcp_port,
			     mcast_addr, mcast_port, mcast_iface,
			     mcast_ttl, loopback, ignore_recreate,
			     ignore_overrun, hb_period, orphan_timeout,
			     max_pkt_age)) ||
	(pub_advert &&
	 (FAILED(advert_create(&adv, adv_addr, adv_port, adv_iface,
			       mcast_ttl, loopback, env, adv_period)) ||
	  FAILED(advert_publish(adv, sndr)))))
	error_report_fatal();

    if (!as_json && tcp_port == 0)
	printf("listening on port %d\n", (int)sender_get_listen_port(sndr));

    if (FAILED(thread_create(&stats_thread, stats_func, NULL)) ||
	FAILED(sender_run(sndr)) ||
	FAILED(thread_stop(stats_thread, &stats_result)) ||
	FAILED(thread_destroy(&stats_thread)) ||
	FAILED((status)(long)stats_result) ||
	FAILED(reporter_destroy(&reporter)) ||
	FAILED(advert_destroy(&adv)) ||
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
