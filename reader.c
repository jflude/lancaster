/* test reader */

#include "clock.h"
#include "datum.h"
#include "error.h"
#include "signals.h"
#include "storage.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>

#define ORPHAN_TIMEOUT_USEC 3000000
#define DISPLAY_DELAY_USEC 200000

#define DUMP_VALUES

static void syntax(const char* prog)
{
	fprintf(stderr, "Syntax: %s [storage file or segment]\n", prog);
	exit(EXIT_FAILURE);
}

int main(int argc, char* argv[])
{
	storage_handle store;
	status st = OK;
	size_t q_capacity;
	long old_head, expected = 0;
	int event = 0;
	microsec last_print, created_time;

	if (argc != 2)
		syntax(argv[0]);

	if (FAILED(signal_add_handler(SIGINT)) || FAILED(signal_add_handler(SIGTERM)) ||
		FAILED(storage_open(&store, argv[1], O_RDONLY)) ||
		FAILED(clock_time(&last_print)))
		error_report_fatal();

#ifndef DUMP_VALUES
	printf("\"%.8s\", ", storage_get_description(store));
#endif

	created_time = storage_get_created_time(store);
	q_capacity = storage_get_queue_capacity(store);
	old_head = storage_get_queue_head(store);

	while (!signal_is_raised(SIGINT) && !signal_is_raised(SIGTERM)) {
		long q, new_head = storage_get_queue_head(store);
		microsec now, last_update = 0;
		if (new_head == old_head) {
			if (FAILED(st = clock_sleep(1)))
				break;
		} else {
			if ((size_t) (new_head - old_head) > q_capacity) {
				old_head = new_head - q_capacity;
				event |= 8;
			}

			for (q = old_head; q < new_head; ++q) {
				record_handle rec;
				struct datum* d;
				version ver;
				microsec ts;
				long xyz;

				identifier id = storage_read_queue(store, q);
				if (id == -1)
					continue;

				if (FAILED(st = storage_get_record(store, id, &rec)) || FAILED(st = clock_time(&now)))
					goto finish;

				d = record_get_value_ref(rec);
				do {
					ver = record_read_lock(rec);
					xyz = d->xyz;
					ts = d->ts;
				} while (ver != record_get_version(rec));

				event |= 1;

				if (xyz < expected)
					event |= 2;
				else
					expected = xyz + 1;

				if (ts < last_update)
					event |= 4;
				else
					last_update = ts;

#ifdef DUMP_VALUES
				printf("%ld %ld %ld\n", ts, id, xyz);
#endif
				old_head = new_head;
			}
		}

		if (storage_get_created_time(store) != created_time) {
			putchar('\n');
			fprintf(stderr, "%s: error: storage \"%s\" recreated\n", argv[0], argv[1]);
			exit(EXIT_FAILURE);
		}

		if (FAILED(clock_time(&now)))
			break;

		if ((now - storage_get_touched_time(store)) >= ORPHAN_TIMEOUT_USEC) {
			putchar('\n');
			fprintf(stderr, "%s: error: storage \"%s\" orphaned\n", argv[0], argv[1]);
			exit(EXIT_FAILURE);
		}

#ifndef DUMP_VALUES
		if ((now - last_print) >= DISPLAY_DELAY_USEC) {
			putchar(event + (event > 9 ? 'A' - 10 : '0'));
			fflush(stdout);
			last_print = now;
			event = 0;
		}
#endif
	}

finish:
	putchar('\n');

	storage_destroy(&store);
	if (FAILED(st) || FAILED(signal_remove_handler(SIGINT)) || FAILED(signal_remove_handler(SIGTERM)))
		error_report_fatal();

	return 0;
}
