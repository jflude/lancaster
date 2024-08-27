/*
  Copyright (c)2014-2017 Peak6 Investments, LP.
  Copyright (c)2018-2024 Justin Flude.
  Use of this source code is governed by the COPYING file.
*/

#include <lancaster/batch.h>
#include <lancaster/error.h>
#include <lancaster/signals.h>
#include <lancaster/xalloc.h>
#include <string.h>

#define STORAGE_CHECK_PERIOD 1000000

struct batch_context {
    q_index head;
    microsec created_time;
};

status batch_read_records(storage_handle store, size_t copy_size,
			  const identifier *ids, void *values, revision *revs,
			  microsec *times, size_t count)
{
    size_t n, val_sz = 0;

    if (count == 0 || !ids ||
	(!values && !revs && !times) || (values && copy_size == 0))
	return error_invalid_arg("batch_read_records");

    if (values) {
	val_sz = storage_get_value_size(store);
	if (copy_size < val_sz)
	    val_sz = copy_size;
    }

    for (n = 0; n < count; ++n) {
	record_handle rec;
	revision rev;
	void *val;
	status st;

	if (FAILED(st = storage_get_record(store, *ids++, &rec)))
	    return st;

	val = record_get_value_ref(rec);
	do {
	    if (FAILED(st = record_read_lock(rec, &rev)))
		return st;

	    if (values)
		memcpy(values, val, val_sz);

	    if (times)
		*times = record_get_timestamp(rec);
	} while (rev != record_get_revision(rec));

	if (revs)
	    *revs++ = rev;

	if (times)
	    ++times;

	if (values)
	    values = (char *)values + copy_size;
    }

    return OK;
}

status batch_write_records(storage_handle store, size_t copy_size,
			   const identifier *ids, const void *values,
			   size_t count)
{
    size_t n, val_sz;
    boolean has_ch_q;

    if (count == 0 || !ids || !values || copy_size == 0)
	return error_invalid_arg("batch_write_records");

    val_sz = storage_get_value_size(store);
    if (copy_size < val_sz)
	val_sz = copy_size;

    has_ch_q = (storage_get_queue_capacity(store) > 0);

    for (n = 0; n < count; ++n) {
	record_handle rec;
	microsec now;
	revision rev;
	void *val;
	status st;

	if (FAILED(st = storage_get_record(store, *ids, &rec)))
	    return st;

	val = record_get_value_ref(rec);
	if (FAILED(st = clock_time(&now)) ||
	    FAILED(st = record_write_lock(rec, &rev)))
	    return st;

	memcpy(val, values, val_sz);

	record_set_timestamp(rec, now);
	record_set_revision(rec, NEXT_REV(rev));

	if (has_ch_q && FAILED(st = storage_write_queue(store, *ids)))
	    return st;

	values = (char *)values + copy_size;
	++ids;
    }

    return OK;
}

static status batch_is_done(microsec begin_time, microsec read_timeout,
                            boolean with_sleep)
{
    status st;
    microsec now;

    if (FAILED(st = signal_any_raised()))
	return st;

    if (read_timeout == 0)
	return TRUE;
    else if (read_timeout < 0)
	return FALSE;

    if ((with_sleep && FAILED(st = clock_sleep(1))) ||
        FAILED(st = clock_time(&now)))
	return st;

    if ((now - begin_time) > read_timeout)
	return TRUE;

    return FALSE;
}

status batch_read_changed_records(storage_handle store, size_t copy_size,
				  identifier *ids, void *values,
				  revision *revs, microsec *times,
				  size_t count, microsec read_timeout,
				  q_index *head)
{
    status st;
    microsec begin_time;
    q_index new_head;
    size_t n, val_sz, q_capacity;

    if (count == 0 || !head || (!ids && !values && !revs && !times) ||
	(values && copy_size == 0))
	return error_invalid_arg("batch_read_changed_records");

    if (read_timeout >= 0 && FAILED(st = clock_time(&begin_time)))
	return st;

    val_sz = storage_get_value_size(store);
    if (copy_size < val_sz)
	val_sz = copy_size;

    if (*head < 0)
	*head = storage_get_queue_head(store);

    q_capacity = storage_get_queue_capacity(store);

    for (n = 0; n < count;) {
	size_t avail, want;
	q_index q;

	for (;;) {
	    new_head = storage_get_queue_head(store);
	    if (new_head != *head)
		break;

	    if (FAILED(st = batch_is_done(begin_time, read_timeout, TRUE)))
		return st;
	    else if (st)
		return (status)n;
	}

	avail = (size_t)(new_head - *head);
	if (avail > q_capacity)
	    return error_msg(CHANGE_QUEUE_OVERRUN,
			     "batch_read_changed_records: "
			     "change queue overrun");

	want = count - n;
	if (avail > want)
	    new_head = *head + want;

	for (q = *head; q < new_head; ++q) {
	    identifier id;
	    record_handle rec;
	    revision rev;
	    void *val;

	    if (FAILED(st = storage_read_queue(store, q, &id)) ||
		FAILED(st = storage_get_record(store, id, &rec)))
		return st;

	    val = record_get_value_ref(rec);
	    do {
		if (FAILED(st = record_read_lock(rec, &rev)))
		    return st;

		if (values)
		    memcpy(values, val, val_sz);

		if (times)
		    *times = record_get_timestamp(rec);
	    } while (rev != record_get_revision(rec));

	    if (revs)
		*revs++ = rev;

	    if (times)
		++times;

	    if (values)
		values = (char *)values + copy_size;

	    if (ids)
		*ids++ = id;
	}

	n += new_head - *head;
	*head = new_head;

	if (FAILED(st = batch_is_done(begin_time, read_timeout, FALSE)))
	    return st;
	else if (st)
	    break;
    }

    return (status)n;
}

status batch_read_changed_records2(storage_handle store, size_t copy_size,
				   identifier *ids, void *values,
				   revision *revs, microsec *times,
				   size_t count, microsec read_timeout,
                                   microsec orphan_timeout,
				   batch_context_handle *pctx)
{
    status st;
    microsec begin_time;
    q_index new_head;
    size_t n, val_sz, q_capacity;

    if (count == 0 || !pctx || (!ids && !values && !revs && !times) ||
	(values && copy_size == 0))
	return error_invalid_arg("batch_read_changed_records2");

    if (read_timeout >= 0 && FAILED(st = clock_time(&begin_time)))
	return st;

    val_sz = storage_get_value_size(store);
    if (copy_size < val_sz)
	val_sz = copy_size;

    if (!*pctx) {
        *pctx = XMALLOC(struct batch_context);
        if (!*pctx)
            return NO_MEMORY;

        (*pctx)->head = storage_get_queue_head(store);
        st = storage_get_created_time(store, &(*pctx)->created_time);
        if (FAILED(st)) {
            XFREE(*pctx);
            return st;
        }
    }

    q_capacity = storage_get_queue_capacity(store);

    for (n = 0; n < count;) {
	size_t avail, want;
	q_index q;
        microsec now, last_storage_check = 0;

	for (;;) {
            if (FAILED(st = clock_time(&now)))
                return st;

            if ((now - last_storage_check) >= STORAGE_CHECK_PERIOD) {
                microsec when;
                last_storage_check = now;

                if (FAILED(st = storage_get_created_time(store, &when)))
                    return st;

                if (when != (*pctx)->created_time)
                    return error_msg(STORAGE_RECREATED,
                                     "batch_read_changed_records2: "
                                     "storage is recreated");

                if (orphan_timeout > 0) {
                    if (FAILED(st = storage_get_touched_time(store, &when)))
                        return st;

                    if ((now - when) >= orphan_timeout)
                        return error_msg(STORAGE_ORPHANED,
                                         "batch_read_changed_records2: "
                                         "storage is orphaned");
                }
            }

	    new_head = storage_get_queue_head(store);
	    if (new_head != (*pctx)->head)
		break;

	    if (FAILED(st = batch_is_done(begin_time, read_timeout, TRUE)))
		return st;
	    else if (st)
		return (status)n;
	}

	avail = (size_t)(new_head - (*pctx)->head);
	if (avail > q_capacity)
	    return error_msg(CHANGE_QUEUE_OVERRUN,
			     "batch_read_changed_records2: "
			     "change queue overrun");

	want = count - n;
	if (avail > want)
	    new_head = (*pctx)->head + want;

	for (q = (*pctx)->head; q < new_head; ++q) {
	    identifier id;
	    record_handle rec;
	    revision rev;
	    void *val;

	    if (FAILED(st = storage_read_queue(store, q, &id)) ||
		FAILED(st = storage_get_record(store, id, &rec)))
		return st;

	    val = record_get_value_ref(rec);
	    do {
		if (FAILED(st = record_read_lock(rec, &rev)))
		    return st;

		if (values)
		    memcpy(values, val, val_sz);

		if (times)
		    *times = record_get_timestamp(rec);
	    } while (rev != record_get_revision(rec));

	    if (revs)
		*revs++ = rev;

	    if (times)
		++times;

	    if (values)
		values = (char *)values + copy_size;

	    if (ids)
		*ids++ = id;
	}

	n += new_head - (*pctx)->head;
	(*pctx)->head = new_head;

	if (FAILED(st = batch_is_done(begin_time, read_timeout, FALSE)))
	    return st;
	else if (st)
	    break;
    }

    return (status)n;
}

status batch_context_destroy(batch_context_handle *pctx)
{
    if (pctx && *pctx)
        XFREE(*pctx);

    return OK;
}
