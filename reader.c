/* test reader */

#include "datum.h"
#include "error.h"
#include "signals.h"
#include "storage.h"
#include "yield.h"
#include <stdio.h>

int main(int argc, char* argv[])
{
	storage_handle store;
	unsigned q_capacity, old_head = 0;
	int n = 0, x = 0;
	char c = '.';

	if (argc != 2) {
		fprintf(stderr, "Syntax: %s [storage file]\n", argv[0]);
		return 1;
	}

	if (FAILED(signal_add_handler(SIGINT)) || FAILED(signal_add_handler(SIGTERM)) ||
		FAILED(storage_open(&store, argv[1])))
		error_report_fatal();

	q_capacity = storage_get_queue_capacity(store);

	while (!signal_is_raised(SIGINT) && !signal_is_raised(SIGTERM)) {
		unsigned q, new_head = storage_get_queue_head(store);
		if (new_head == old_head) {
			snooze(0, 1000);
			continue;
		}

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

			if (FAILED(storage_lookup(store, id, &rec)))
				error_report_fatal();

			d = record_get_value(rec);
			do {
				seq = record_read_lock(rec);
				bid_qty = d->bidSize;
				ask_qty = d->askSize;
			} while (seq != record_get_sequence(rec));

			if (c == '.' && (bid_qty != n || ask_qty != n + 1))
				c = '!';

			n = ask_qty + 1;
		}

		old_head = new_head;

		if ((x++ & 1023) == 0) {
			putchar(c);
			fflush(stdout);
			c = '.';
		}
	}

	storage_destroy(&store);

	if (FAILED(signal_remove_handler(SIGINT)) || FAILED(signal_remove_handler(SIGTERM)))
		error_report_fatal();

	return 0;
}
