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

status streamer_read_value(streamer_handle stream, identifier id, void* buf,
						   size_t buf_size, microsec *when)
{
	status st;
	record_handle rec;
	revision rev;
	void *val;
	size_t rec_sz;

	if (!buf)
		return error_invalid_arg("streamer_get_value");

	rec_sz = storage_get_value_size(stream->store);
	if (buf_size < rec_sz)
		return error_msg("streamer_get_value: buffer too small",
						 BUFFER_TOO_SMALL);

	if (FAILED(st = storage_get_record(stream->store, id, &rec)))
		return st;

	val = record_get_value_ref(rec);
	do {
		if (FAILED(st = record_read_lock(rec, &rev)))
			return st;

		memcpy(buf, val, rec_sz);
		if (when)
			*when = record_get_timestamp(rec);
	} while (rev != record_get_revision(rec));

	return OK;
}

status streamer_read_next_value(streamer_handle stream, void *buf,
								size_t buf_size, microsec *when)
{
	status st;
	q_index new_head;
	identifier id;

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
		return error_msg("streamer_get_next_value: change queue overrun",
						 CHANGE_QUEUE_OVERRUN);

	if (FAILED(st = storage_read_queue(stream->store, stream->old_head, &id)) ||
		FAILED(st = streamer_read_value(stream, id, buf, buf_size, when)))
		return st;

	++stream->old_head;
	return OK;
}
