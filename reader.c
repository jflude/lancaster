/* test reader */

#include "clock.h"
#include "datum.h"
#include "error.h"
#include "signals.h"
#include "storage.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>

#define ORPHAN_TIMEOUT_USEC (3 * 1000000)
#define DISPLAY_DELAY_USEC (0.2 * 1000000)

static storage_handle store;
int event;

static void syntax(const char* prog)
{
	fprintf(stderr, "Syntax: %s [storage file or segment]\n", prog);
	exit(EXIT_FAILURE);
}

static status update(queue_index q)
{
	record_handle rec;
	struct datum* d;
	version ver;
	long xyz;
	status st;
	identifier id;

	if (signal_is_raised(SIGHUP) ||
		signal_is_raised(SIGINT) ||
		signal_is_raised(SIGTERM))
		return FALSE;

	id = storage_read_queue(store, q);
	if (id == -1)
		return TRUE;

	if (FAILED(st = storage_get_record(store, id, &rec)))
		return st;

	d = record_get_value_ref(rec);
	do {
		ver = record_read_lock(rec);
		xyz = d->xyz;
	} while (ver != record_get_version(rec));

	event |= 1;

	if (q > xyz)
		event |= 2;

	return TRUE;
}

int main(int argc, char* argv[])
{
	status st = OK;
	size_t q_capacity;
	long old_head;
	microsec last_print, created_time;

	error_set_program_name(argv[0]);

	if (argc != 2)
		syntax(argv[0]);

	if (FAILED(signal_add_handler(SIGHUP)) ||
		FAILED(signal_add_handler(SIGINT)) ||
		FAILED(signal_add_handler(SIGTERM)) ||
		FAILED(storage_open(&store, argv[1], O_RDONLY)) ||
		FAILED(clock_time(&last_print)))
		error_report_fatal();

	printf("\"%.20s\", ", storage_get_description(store));

	created_time = storage_get_created_time(store);
	q_capacity = storage_get_queue_capacity(store);
	old_head = storage_get_queue_head(store);

	while (!signal_is_raised(SIGINT) && !signal_is_raised(SIGTERM)) {
		queue_index q, new_head = storage_get_queue_head(store);
		microsec now;
		if (new_head == old_head) {
			if (FAILED(st = clock_sleep(1)))
				break;
		} else {
			if ((size_t) (new_head - old_head) > q_capacity) {
				old_head = new_head - q_capacity;
				event |= 4;
			}

			for (q = old_head; q < new_head; ++q)
				if (FAILED(st = update(q)) || !st)
					goto finish;

			old_head = new_head;
		}

		if (storage_get_created_time(store) != created_time) {
			putchar('\n');
			fprintf(stderr, "%s: main: storage is recreated\n", argv[0]);
			exit(EXIT_FAILURE);
		}

		if (FAILED(clock_time(&now)))
			break;

		if ((now - storage_get_touched_time(store)) >= ORPHAN_TIMEOUT_USEC) {
			putchar('\n');
			fprintf(stderr, "%s: main: storage is orphaned\n", argv[0]);
			exit(EXIT_FAILURE);
		}

		if ((now - last_print) >= DISPLAY_DELAY_USEC) {
			putchar(event + (event > 9 ? 'A' - 10 : '0'));
			fflush(stdout);
			last_print = now;
			event = 0;
		}
	}

finish:
	storage_destroy(&store);

	putchar('\n');

	if (FAILED(st) ||
		FAILED(signal_remove_handler(SIGHUP)) ||
		FAILED(signal_remove_handler(SIGINT)) ||
		FAILED(signal_remove_handler(SIGTERM)))
		error_report_fatal();

	return 0;
}
