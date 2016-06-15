#include <string.h>
#include "error.h"
#include "signals.h"
#include "streamer.h"
#include "xalloc.h"

struct streamer {
	storage_handle store;
	q_index old_head;
};

status streamer_create(streamer_handle *pstream, storage_handle store)
{
	if (!pstream)
		return error_invalid_arg("streamer_create");

	*pstream = XMALLOC(struct streamer);
	if (!*pstream)
		return NO_MEMORY;

	(*pstream)->store = store;
	(*pstream)->old_head = storage_get_queue_head(store);

	 return OK;
}

status streamer_destroy(streamer_handle *pstream)
{
	if (pstream && *pstream)
		XFREE(*pstream);

	return OK;
}

status streamer_read_next_value(streamer_handle stream, identifier *id,
								void *value, size_t value_size,
								revision *rev, microsec *when)
{
	status st;
	q_index new_head;
	identifier i;

	if (!id && !value && !rev && !when)
		return error_invalid_arg("streamer_read_next_value");

	for (;;) {
		new_head = storage_get_queue_head(stream->store);
		if (new_head != stream->old_head)
			break;

		if (FAILED(st = clock_sleep(1)) ||
			FAILED(st = signal_any_raised()))
			return st;
	}

	if ((size_t)(new_head - stream->old_head) >
		storage_get_queue_capacity(stream->store))
		return error_msg("streamer_read_next_value: change queue overrun",
						 CHANGE_QUEUE_OVERRUN);

	if (FAILED(st = storage_read_queue(stream->store, stream->old_head, &i)) ||
		((value || rev || when) &&
		 FAILED(st = storage_read_value(stream->store, i, value,
										value_size, rev, when))))
		return st;

	if (i)
		*id = i;

	++stream->old_head;
	return OK;
}
