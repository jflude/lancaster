/* test reader */

#include "clock.h"
#include "datum.h"
#include "error.h"
#include "latency.h"
#include "signals.h"
#include "storage.h"
#include "version.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define ORPHAN_TIMEOUT_USEC (3 * 1000000)
#define DISPLAY_DELAY_USEC (0.2 * 1000000)
#define QUEUE_DELAY_USEC (1 * 1000000)

static storage_handle store;
static latency_handle stg_latency;
static int event;

static void show_syntax(void)
{
	fprintf(stderr, "Syntax: %s [-v] [-p ERROR PREFIX] [-s] STORAGE-FILE\n",
			error_get_program_name());

	exit(-SYNTAX_ERROR);
}

static void show_version(void)
{
	printf("reader %s\n", version_get_source());
	exit(0);
}

static status update(q_index qi)
{
	record_handle rec = NULL;
	identifier id;
	microsec now, when;
	struct datum* d;
	revision rev;
	long xyz;
	status st;

	if (FAILED(st = signal_is_raised(SIGHUP)) ||
		FAILED(st = signal_is_raised(SIGINT)) ||
		FAILED(st = signal_is_raised(SIGTERM)) ||
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

	event |= 1;
	if (qi > xyz)
		event |= 2;

	if (stg_latency && !FAILED(st = clock_time(&now)))
		st = latency_on_sample(stg_latency, now - when);

	return st;
}

int main(int argc, char* argv[])
{
	status st = OK;
	size_t q_capacity;
	long old_head;
	int opt;
	boolean stg_stats = FALSE;
	microsec last_print, created_time, delay;
	const char* eol_seq;

	char prog_name[256];
	strcpy(prog_name, argv[0]);
	error_set_program_name(prog_name);

	while ((opt = getopt(argc, argv, "p:sv")) != -1)
		switch (opt) {
		case 'p':
			strcat(prog_name, ": ");
			strcat(prog_name, optarg);
			error_set_program_name(prog_name);
			break;
		case 's':
			stg_stats = TRUE;
			break;
		case 'v':
			show_version();
		default:
			show_syntax();
		}

	if ((argc - optind) != 1)
		show_syntax();

	if (FAILED(signal_add_handler(SIGHUP)) ||
		FAILED(signal_add_handler(SIGINT)) ||
		FAILED(signal_add_handler(SIGTERM)) ||
		FAILED(storage_open(&store, argv[optind], O_RDONLY)) ||
		FAILED(storage_get_created_time(store, &created_time)) ||
		(stg_stats && FAILED(latency_create(&stg_latency))) ||
		FAILED(clock_time(&last_print)))
		error_report_fatal();

	if (strcmp(storage_get_description(store), "TEST") != 0) {
		fprintf(stderr, "%s: main: unrecognized storage description: \"%s\"\n",
				error_get_program_name(), storage_get_description(store));

		exit(-STORAGE_CORRUPTED);
	}

	if (!stg_stats)
		printf("\"%.20s\", ", storage_get_description(store));

	eol_seq = (isatty(STDOUT_FILENO) ? "\033[K\r" : "\n");

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
			if ((size_t) (new_head - old_head) > q_capacity) {
				old_head = new_head - q_capacity;
				event |= 4;
			}

			for (q = old_head; q < new_head; ++q)
				if (FAILED(st = update(q)))
					goto finish;

			old_head = new_head;
		}

		if (FAILED(st = signal_is_raised(SIGHUP)) ||
			FAILED(st = signal_is_raised(SIGINT)) ||
			FAILED(st = signal_is_raised(SIGTERM)) ||
			FAILED(st = storage_get_created_time(store, &when)))
			break;

		if (when != created_time) {
			putchar('\n');
			fprintf(stderr, "%s: main: storage is recreated\n",
					error_get_program_name());

			exit(-STORAGE_RECREATED);
		}

		if (FAILED(clock_time(&now)) ||
			FAILED(storage_get_touched_time(store, &when)))
			break;

		if ((now - when) >= ORPHAN_TIMEOUT_USEC) {
			putchar('\n');
			fprintf(stderr, "%s: main: storage is orphaned\n",
					error_get_program_name());

			exit(-STORAGE_ORPHANED);
		}

		if ((now - last_print) >= delay) {
			if (stg_stats) {
				double secs = (now - last_print) / 1000000.0;
				printf("\"%.20s\", STG.REC/s: %.2f, STG.MIN/us: %.2f, "
					   "STG.AVG/us: %.2f, STG.MAX/us: %.2f, STG.STD/us: %.2f "
					   "[%c]%s",
					   storage_get_description(store),
					   latency_get_count(stg_latency) / secs,
					   latency_get_min(stg_latency),
					   latency_get_mean(stg_latency),
					   latency_get_max(stg_latency),
					   latency_get_stddev(stg_latency),
					   event + (event > 9 ? 'A' - 10 : '0'),
					   eol_seq);

				if (FAILED(st = latency_roll(stg_latency)))
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
		FAILED(latency_destroy(&stg_latency)) ||
		FAILED(signal_remove_handler(SIGHUP)) ||
		FAILED(signal_remove_handler(SIGINT)) ||
		FAILED(signal_remove_handler(SIGTERM)))
		error_report_fatal();

	return 0;
}
