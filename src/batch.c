/*
   Copyright (c)2014-2017 Peak6 Investments, LP.  All rights reserved.
   Use of this source code is governed by the LICENSE file.
*/

#include <string.h>
#include "batch.h"
#include "error.h"
#include "signals.h"

status batch_read_records(storage_handle store, size_t copy_size,
			  const identifier * ids, void *values, revision * revs,
			  microsec * times, size_t count)
{
    size_t val_sz, n;

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
	    values = (char *) values + copy_size;
    }

    return OK;
}

status batch_write_records(storage_handle store, size_t copy_size,
			   const identifier * ids, const void *values,
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

	values = (char *) values + copy_size;
	++ids;
    }

    return OK;
}

static status check_done(microsec then, microsec timeout, boolean sleep)
{
    status st;
    microsec now;

    if (FAILED(st = signal_any_raised()))
	return st;

    if (timeout == 0)
	return TRUE;
    else if (timeout < 0)
	return FALSE;

    if ((sleep && FAILED(st = clock_sleep(1))) ||
	FAILED(st = clock_time(&now)))
	return st;

    if ((now - then) > timeout)
	return TRUE;

    return FALSE;
}

status batch_read_changed_records(storage_handle store, size_t copy_size,
				  identifier * ids, void *values,
				  revision * revs, microsec * times,
				  size_t count, microsec timeout,
				  q_index * head)
{
    status st;
    microsec then;
    q_index new_head;
    size_t n, val_sz, q_capacity;

    if (count == 0 || !head || (!ids && !values && !revs && !times) ||
	(values && copy_size == 0))
	return error_invalid_arg("batch_read_changed_records");

    if (timeout >= 0 && FAILED(st = clock_time(&then)))
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

	    if (FAILED(st = check_done(then, timeout, TRUE)))
		return st;
	    else if (st)
		return (status) n;
	}

	avail = (size_t) (new_head - *head);
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
		values = (char *) values + copy_size;

	    if (ids)
		*ids++ = id;
	}

	n += new_head - *head;
	*head = new_head;

	if (FAILED(st = check_done(then, timeout, FALSE)))
	    return st;
	else if (st)
	    break;
    }

    return (status) n;
}
