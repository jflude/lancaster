/*
  Copyright (c)2014-2017 Peak6 Investments, LP.  All rights reserved.
  Use of this source code is governed by the COPYING file.
*/

/* test reader */

#include <lancaster/a2i.h>
#include <lancaster/clock.h>
#include <lancaster/datum.h>
#include <lancaster/error.h>
#include <lancaster/latency.h>
#include <lancaster/signals.h>
#include <lancaster/storage.h>
#include <lancaster/version.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define DISPLAY_DELAY_USEC (0.2 * 1000000)
#define DEFAULT_ORPHAN_TIMEOUT_USEC (3 * 1000000)
#define QUEUE_DELAY_USEC (1 * 1000000)

#define DATA_UPDATED 1
#define DATA_SKIPPED 2
#define QUEUE_OVERRUN 4

static storage_handle store;
static latency_handle stg_latency;
static int event;

static void show_syntax(void)
{
    fprintf(stderr, "Syntax: %s [-v] [-L] [-O ORPHAN-TIMEOUT] "
	    "[-p ERROR PREFIX] [-Q] [-R] [-s] STORAGE-FILE\n",
	    error_get_program_name());

    exit(-SYNTAX_ERROR);
}

static status output_stg(double secs)
{
    if (printf("\"%.20s\", STG.REC/s: %.2f, STG.MIN/us: %.2f, "
	       "STG.AVG/us: %.2f, STG.MAX/us: %.2f, STG.STD/us: %.2f "
	       "[%c]%s",
	       storage_get_description(store),
	       latency_get_count(stg_latency) / secs,
	       latency_get_min(stg_latency),
	       latency_get_mean(stg_latency),
	       latency_get_max(stg_latency),
	       latency_get_stddev(stg_latency),
	       event + (event > 9 ? 'A' - 10 : '0'),
	       (isatty(STDOUT_FILENO) ? "\033[K\r" : "\n")) < 0)
	return error_errno("printf");

    return OK;
}

static status update(q_index qi)
{
    record_handle rec = NULL;
    identifier id;
    microsec now, when;
    struct datum *d;
    revision rev;
    long xyz;
    status st;

    if (FAILED(st = signal_any_raised()) ||
	FAILED(st = storage_read_queue(store, qi, &id)) ||
	FAILED(st = storage_get_record(store, id, &rec)))
	return st;

    d = record_get_value_ref(rec);
    do {
	if (FAILED(st = record_read_lock(rec, &rev)))
	    return st;

	xyz = d->xyz;
	when = record_get_timestamp(rec);
    } while (rev != record_get_revision(rec));

    event |= DATA_UPDATED;
    if (qi > xyz)
	event |= DATA_SKIPPED;

    if (stg_latency && !FAILED(st = clock_time(&now)))
	st = latency_on_sample(stg_latency, now - when);

    return st;
}

int main(int argc, char *argv[])
{
    status st = OK;
    size_t q_capacity;
    long old_head;
    boolean stg_stats = FALSE, ignore_recreate = FALSE, ignore_overrun = FALSE;
    microsec last_print, created_time, delay,
	orphan_timeout = DEFAULT_ORPHAN_TIMEOUT_USEC;
    int opt;

    char prog_name[256];
    strcpy(prog_name, argv[0]);
    error_set_program_name(prog_name);

    while ((opt = getopt(argc, argv, "LO:p:QRsv")) != -1)
	switch (opt) {
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
	case 'Q':
	    ignore_overrun = TRUE;
	    break;
	case 'R':
	    ignore_recreate = TRUE;
	    break;
	case 's':
	    stg_stats = TRUE;
	    break;
	case 'v':
	    show_version("reader");
	    /* fall through */
	default:
	    show_syntax();
	}

    if ((argc - optind) != 1)
	show_syntax();

    if (FAILED(signal_add_handler(SIGHUP)) ||
	FAILED(signal_add_handler(SIGINT)) ||
	FAILED(signal_add_handler(SIGTERM)) ||
	FAILED(storage_open(&store, argv[optind], O_RDONLY)) ||
	(!ignore_recreate &&
	 FAILED(storage_get_created_time(store, &created_time))) ||
	(stg_stats && FAILED(latency_create(&stg_latency))) ||
	FAILED(clock_time(&last_print)))
	error_report_fatal();

    if (strcmp(storage_get_description(store), "TEST") != 0) {
	error_msg(WRONG_DATA_VERSION,
		  "error: unrecognized storage description: \"%s\"",
		  storage_get_description(store));

	error_report_fatal();
    }

    if (!stg_stats)
	printf("\"%.20s\", ", storage_get_description(store));

    q_capacity = storage_get_queue_capacity(store);
    old_head = storage_get_queue_head(store);
    delay = (stg_stats ? QUEUE_DELAY_USEC : DISPLAY_DELAY_USEC);

    for (;;) {
	microsec now, when;
	q_index q, new_head = storage_get_queue_head(store);

	if (new_head == old_head) {
	    if (FAILED(st = clock_sleep(1)))
		break;
	} else {
	    if ((size_t)(new_head - old_head) > q_capacity) {
		if (ignore_overrun) {
		    old_head = new_head - q_capacity;
		    event |= QUEUE_OVERRUN;
		} else {
		    putchar('\n');
		    error_msg(CHANGE_QUEUE_OVERRUN,
			      "error: change queue overrun");
		    error_report_fatal();
		}
	    }

	    for (q = old_head; q < new_head; ++q)
		if (FAILED(st = update(q)))
		    goto finish;

	    old_head = new_head;
	}

	if (FAILED(st = signal_any_raised()))
	    break;

	if (!ignore_recreate) {
	    if (FAILED(st = storage_get_created_time(store, &when)))
		break;

	    if (when != created_time) {
		putchar('\n');
		error_msg(STORAGE_RECREATED, "error: storage is recreated");
		error_report_fatal();
	    }
	}

	if (FAILED(clock_time(&now)) ||
	    FAILED(storage_get_touched_time(store, &when)))
	    break;

	if (orphan_timeout > 0 && (now - when) >= orphan_timeout) {
	    putchar('\n');
	    error_msg(STORAGE_ORPHANED, "error: storage is orphaned");
	    error_report_fatal();
	}

	if ((now - last_print) >= delay) {
	    if (stg_stats) {
		if (FAILED(st = output_stg((now - last_print) / 1000000.0)) ||
		    FAILED(st = latency_roll(stg_latency)))
		    break;
	    } else
		putchar(event + (event > 9 ? 'A' - 10 : '0'));

	    event = 0;
	    last_print = now;
	    fflush(stdout);
	}
    }

finish:
    putchar('\n');

    if (FAILED(st) ||
	FAILED(storage_destroy(&store)) ||
	(stg_stats && FAILED(latency_destroy(&stg_latency))) ||
	FAILED(signal_remove_handler(SIGHUP)) ||
	FAILED(signal_remove_handler(SIGINT)) ||
	FAILED(signal_remove_handler(SIGTERM)))
	error_report_fatal();

    return 0;
}
