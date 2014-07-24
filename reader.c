/* test reader */

#include "datum.h"
#include "error.h"
#include "signals.h"
#include "storage.h"
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
		unsigned j, new_head = storage_get_queue_head(store);
		if (new_head == old_head) {
			snooze();
			continue;
		}

		if ((new_head - old_head) > q_capacity) {
			old_head = new_head - q_capacity;
			c = '*';
		}

		for (j = old_head; j < new_head; ++j) {
			record_handle rec;
			struct datum_t* d;
			int new_n;

			int id = storage_read_queue(store, j);
			if (id == -1)
				continue;

			if (FAILED(storage_lookup(store, id, &rec)))
				error_report_fatal();

			d = record_get_value(rec);

			RECORD_LOCK(rec);
			new_n = d->bid_qty;
			RECORD_UNLOCK(rec);

			if (new_n != (n + 1) && c == '.')
				c = '!';

			n = new_n;
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
