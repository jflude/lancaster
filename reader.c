/* test reader */

#include "clock.h"
#include "datum.h"
#include "error.h"
#include "signals.h"
#include "storage.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define DISPLAY_DELAY_USEC 200000
#define STALE_TIMEOUT_USEC 5000000

int main(int argc, char* argv[])
{
	storage_handle store;
	status st = OK;
	unsigned q_capacity, old_head = 0;
	int n = 0;
	char c = ' ';
	microsec_t t1, t2, creation_time;

	if (argc != 2) {
		fprintf(stderr, "Syntax: %s [storage file or segment]\n", argv[0]);
		return 1;
	}

	if (FAILED(signal_add_handler(SIGINT)) ||
		FAILED(signal_add_handler(SIGTERM)) ||
		FAILED(storage_open(&store, argv[1])) ||
		FAILED(clock_time(&t1)))
		error_report_fatal();

	q_capacity = storage_get_queue_capacity(store);
	creation_time = storage_get_creation_time(store);

	while (!signal_is_raised(SIGINT) && !signal_is_raised(SIGTERM)) {
		unsigned q, new_head = storage_get_queue_head(store);
		if (new_head == old_head) {
			if (FAILED(st = clock_sleep(1)))
				break;
		} else {
			if ((new_head - old_head) > q_capacity) {
				old_head = new_head - q_capacity;
				c = '*';
			}

			for (q = old_head; q < new_head; ++q) {
				record_handle rec;
				struct datum_t* d;
				sequence seq;
				int bid_qty, ask_qty;

				int id = storage_read_queue(store, q);
				if (id == -1)
					continue;

				if (FAILED(st = storage_get_record(store, id, &rec)))
					goto finish;

				d = record_get_value_ref(rec);
				do {
					seq = record_read_lock(rec);
					bid_qty = d->bidSize;
					ask_qty = d->askSize;
				} while (seq != record_get_sequence(rec));

				if (bid_qty == n && ask_qty == n + 1) {
					if (c == ' ')
						c = '.';
				} else {
					if (c != '*')
						c = '!';
				}

				n = ask_qty + 1;
			}

			old_head = new_head;
		}

		if (storage_get_creation_time(store) != creation_time) {
			putchar('\n');
			fprintf(stderr, "%s: error: storage \"%s\" is recreated\n", argv[0], argv[1]);
			exit(EXIT_FAILURE);
		}

		if (FAILED(clock_time(&t2)))
			break;

		if ((t2 - storage_get_send_recv_time(store)) > STALE_TIMEOUT_USEC) {
			putchar('\n');
			fprintf(stderr, "%s: error: storage \"%s\" is stale\n", argv[0], argv[1]);
			exit(EXIT_FAILURE);
		}

		if ((t2 - t1) > DISPLAY_DELAY_USEC) {
			putchar(c);
			fflush(stdout);

			t1 = t2;
			c = ' ';
		}
	}

finish:
	putchar('\n');

	storage_destroy(&store);
	if (FAILED(st) ||
		FAILED(signal_remove_handler(SIGINT)) ||
		FAILED(signal_remove_handler(SIGTERM)))
		error_report_fatal();

	return 0;
}
