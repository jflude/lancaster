/* test reader */

#include "clock.h"
#include "datum.h"
#include "error.h"
#include "signals.h"
#include "storage.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ORPHAN_TIMEOUT_USEC (3 * 1000000)
#define DISPLAY_DELAY_USEC (0.2 * 1000000)

static storage_handle store;
int event;

static void show_syntax(void)
{
	fprintf(stderr, "Syntax: %s [-v] STORAGE-FILE\n", error_get_program_name());
	exit(1);
}

static void show_version(void)
{
	printf("reader 1.0\n");
	exit(0);
}

static status update(queue_index q)
{
	record_handle rec;
	struct datum* d;
	version ver;
	long xyz;
	status st;
	identifier id;

	if (FAILED(st = signal_is_raised(SIGHUP)) ||
		FAILED(st = signal_is_raised(SIGINT)) ||
		FAILED(st = signal_is_raised(SIGTERM)))
		return st;

	id = storage_read_queue(store, q);
	if (id == -1)
		return OK;

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

	return OK;
}

int main(int argc, char* argv[])
{
	status st = OK;
	size_t q_capacity;
	long old_head;
	microsec last_print, created_time;

	error_set_program_name(argv[0]);

	if (argc != 2)
		show_syntax();

	if (strcmp(argv[1], "-v") == 0)
		show_version();

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

	for (;;) {
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
				if (FAILED(st = update(q)))
					goto finish;

			old_head = new_head;
		}

		if (FAILED(st = signal_is_raised(SIGHUP)) ||
			FAILED(st = signal_is_raised(SIGINT)) ||
			FAILED(st = signal_is_raised(SIGTERM)))
			break;

		if (storage_get_created_time(store) != created_time) {
			putchar('\n');
			fprintf(stderr, "%s: main: storage is recreated\n", argv[0]);
			exit(-STORAGE_RECREATED);
		}

		if (FAILED(clock_time(&now)))
			break;

		if ((now - storage_get_touched_time(store)) >= ORPHAN_TIMEOUT_USEC) {
			putchar('\n');
			fprintf(stderr, "%s: main: storage is orphaned\n", argv[0]);
			exit(-STORAGE_ORPHANED);
		}

		if ((now - last_print) >= DISPLAY_DELAY_USEC) {
			putchar(event + (event > 9 ? 'A' - 10 : '0'));
			fflush(stdout);
			last_print = now;
			event = 0;
		}
	}

finish:
	putchar('\n');

	if (FAILED(st) ||
		FAILED(storage_destroy(&store)) ||
		FAILED(signal_remove_handler(SIGHUP)) ||
		FAILED(signal_remove_handler(SIGINT)) ||
		FAILED(signal_remove_handler(SIGTERM)))
		error_report_fatal();

	return 0;
}
