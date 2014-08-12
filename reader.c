/* test reader */

#include "clock.h"
#include "datum.h"
#include "error.h"
#include "signals.h"
#include "storage.h"
#include <stdio.h>
#include <stdlib.h>

#define STALE_DATA_USEC 1000000
#define DISPLAY_DELAY_USEC 200000
#define ORPHAN_TIMEOUT_USEC 3000000

int main(int argc, char* argv[])
{
	storage_handle store;
	status st = OK;
	size_t q_capacity;
	long old_head, expected = 0;
	char c = ' ';
	microsec_t last_print, creation_time;

	if (argc != 2) {
		fprintf(stderr, "Syntax: %s [storage file or segment]\n", argv[0]);
		return 1;
	}

	if (FAILED(signal_add_handler(SIGINT)) || FAILED(signal_add_handler(SIGTERM)) ||
		FAILED(storage_open(&store, argv[1])) || FAILED(clock_time(&last_print)))
		error_report_fatal();

	creation_time = storage_get_creation_time(store);

	q_capacity = storage_get_queue_capacity(store);
	old_head = storage_get_queue_head(store);

	printf("\"%.8s\" ", storage_get_description(store));

	while (!signal_is_raised(SIGINT) && !signal_is_raised(SIGTERM)) {
		long q, new_head = storage_get_queue_head(store);
		microsec_t now;
		if (new_head == old_head) {
			if (FAILED(st = clock_sleep(1)))
				break;
		} else {
			if ((size_t) (new_head - old_head) > q_capacity) {
				old_head = new_head - q_capacity;
				c = '*';
			}

			for (q = old_head; q < new_head; ++q) {
				record_handle rec;
				struct datum_t* d;
				sequence seq;
				microsec_t diff_ts;
				long xyz;

				identifier id = storage_read_queue(store, q);
				if (id == -1)
					continue;

				if (FAILED(st = storage_get_record(store, id, &rec)) || FAILED(st = clock_time(&now)))
					goto finish;

				d = record_get_value_ref(rec);
				do {
					seq = record_read_lock(rec);
					xyz = d->xyz;
					diff_ts = now - d->ts;
				} while (seq != record_get_sequence(rec));

				if (diff_ts >= STALE_DATA_USEC) {
					if (c != '*')
						c = '!';
				} else if (xyz == expected) {
					if (c == ' ')
						c = '.';
				} else if (c != '*')
					c = '?';

				expected = xyz + 1;
			}

			old_head = new_head;
		}

		if (storage_get_creation_time(store) != creation_time) {
			putchar('\n');
			fprintf(stderr, "%s: error: storage \"%s\" is recreated\n", argv[0], argv[1]);
			exit(EXIT_FAILURE);
		}

		if (FAILED(clock_time(&now)))
			break;

		if ((now - storage_get_send_recv_time(store)) >= ORPHAN_TIMEOUT_USEC) {
			putchar('\n');
			fprintf(stderr, "%s: error: storage \"%s\" is orphaned\n", argv[0], argv[1]);
			exit(EXIT_FAILURE);
		}

		if ((now - last_print) >= DISPLAY_DELAY_USEC) {
			putchar(c);
			fflush(stdout);
			last_print = now;
			c = ' ';
		}
	}

finish:
	putchar('\n');

	storage_destroy(&store);
	if (FAILED(st) || FAILED(signal_remove_handler(SIGINT)) || FAILED(signal_remove_handler(SIGTERM)))
		error_report_fatal();

	return 0;
}
