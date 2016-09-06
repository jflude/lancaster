/*
   Copyright (c)2014-2016 Peak6 Investments, LP.  All rights reserved.
   Use of this source code is governed by the LICENSE file.
*/

/* generic subscriber */

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include "a2i.h"
#include "error.h"
#include "clock.h"
#include "receiver.h"
#include "reporter.h"
#include "signals.h"
#include "socket.h"
#include "thread.h"
#include "version.h"

#define DEFAULT_TOUCH_USEC (1 * 1000000)
#define DISPLAY_DELAY_USEC (1 * 1000000)
#define STORAGE_PERM (S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH)

static receiver_handle rcvr;
static reporter_handle reporter;
static char hostname[256];
static boolean as_json;

static void show_syntax(void)
{
    fprintf(stderr, "Syntax: %s [-v] [-H MAX-MISSED-HEARTBEATS] [-j] [-L] "
	    "[-p ERROR PREFIX] [-q CHANGE-QUEUE-CAPACITY] "
	    "[-S STATISTICS-UDP-ADDRESS:PORT] [-T TOUCH-PERIOD] STORAGE-FILE "
	    "TCP-ADDRESS:PORT\n", error_get_program_name());

    exit(-SYNTAX_ERROR);
}

static status output_json(double secs, microsec now, const char *alias)
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
    char alias[32];
    microsec last_print;
    status st;

    const char *storage_desc =
	storage_get_description(receiver_get_storage(rcvr));

    if (as_json) {
	const char *delim_pos = strchr(storage_desc, '.');
	if (!delim_pos)
	    strncpy(alias, "unknown", sizeof(alias));
	else
	    strncpy(alias, storage_desc, delim_pos - storage_desc);
    }

    if (FAILED(st = clock_time(&last_print))) {
	receiver_stop(rcvr);
	return (void *) (long) st;
    }

    while (!thread_is_stopping(thr)) {
	microsec now;
	double secs;
	if (FAILED(st = signal_any_raised()) ||
	    FAILED(st = clock_sleep(DISPLAY_DELAY_USEC)) ||
	    FAILED(st = clock_time(&now)))
	    break;

	secs = (now - last_print) / 1000000.0;

	if (as_json)
	    output_json(secs, now, alias);
	else
	    output_std(secs);

	if (FAILED(st = receiver_roll_stats(rcvr)))
	    break;

	last_print = now;

	if (!reporter && fflush(stdout) == -1) {
	    st = error_errno("fflush");
	    break;
	}
    }

    if (!reporter)
	putchar('\n');

    receiver_stop(rcvr);
    return (void *) (long) st;
}

int main(int argc, char *argv[])
{
    thread_handle stats_thread;
    const char *mmap_file;
    char tcp_addr[64], stats_addr[64];
    unsigned short tcp_port, stats_port;
    size_t q_capacity = SENDER_QUEUE_CAPACITY;
    microsec touch_period = DEFAULT_TOUCH_USEC;
    unsigned max_missed_hb = 5;
    void *stats_result;
    int opt;

    char prog_name[256];
    strcpy(prog_name, argv[0]);
    error_set_program_name(prog_name);

    while ((opt = getopt(argc, argv, "H:jLp:q:S:T:v")) != -1)
	switch (opt) {
	case 'H':
	    if (FAILED(a2i(optarg, "%u", &max_missed_hb)))
		error_report_fatal();
	    break;
	case 'j':
	    if (FAILED(sock_get_hostname(hostname, sizeof(hostname))))
		error_report_fatal();

	    as_json = TRUE;
	    break;
	case 'L':
	    error_with_timestamp(TRUE);
	    break;
	case 'p':
	    strcat(prog_name, ": ");
	    strcat(prog_name, optarg);
	    error_set_program_name(prog_name);
	    break;
	case 'q':
	    if (FAILED(a2i(optarg, "%lu", &q_capacity)))
		error_report_fatal();
	    break;
	case 'S':
	    if (FAILED(sock_addr_split(optarg, stats_addr,
				       sizeof(stats_addr), &stats_port)) ||
		FAILED(reporter_create(&reporter, stats_addr, stats_port)))
		error_report_fatal();
	    break;
	case 'T':
	    if (FAILED(a2i(optarg, "%ld", &touch_period)))
		error_report_fatal();
	    break;
	case 'v':
	    show_version();
	default:
	    show_syntax();
	}

    if ((argc - optind) != 2)
	show_syntax();

    mmap_file = argv[optind++];

    if (FAILED(sock_addr_split(argv[optind++], tcp_addr,
			       sizeof(tcp_addr), &tcp_port)) ||
	FAILED(signal_add_handler(SIGHUP)) ||
	FAILED(signal_add_handler(SIGINT)) ||
	FAILED(signal_add_handler(SIGTERM)) ||
	FAILED(receiver_create(&rcvr, mmap_file, STORAGE_PERM, 0,
			       q_capacity, touch_period, max_missed_hb,
			       tcp_addr, tcp_port)) ||
	FAILED(thread_create(&stats_thread, stats_func, NULL)) ||
	FAILED(receiver_run(rcvr)) ||
	FAILED(thread_stop(stats_thread, &stats_result)) ||
	FAILED(thread_destroy(&stats_thread)) ||
	FAILED((status) (long) stats_result) ||
	FAILED(reporter_destroy(&reporter)) ||
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
