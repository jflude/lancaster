#include "storage.h"
#include "error.h"
#include "xalloc.h"

struct record_t
{
	spin_lock_t lock;
	int id;
	long seq;
	char val[1];
};

struct storage_t
{
	record_handle array;
	record_handle limit;
	size_t record_size;
	size_t val_size;
	int base_id;
	int max_id;
};

#define RECORD_ADDR(stg, base, idx) ((record_handle) ((char*) base + (idx) * (stg)->record_size))

status storage_create(storage_handle* pstore, int base_id, int max_id, size_t val_size)
{
	int i;
	record_handle rec;
	if (!pstore || base_id < 0 || max_id < (base_id + 1) || val_size == 0) {
		error_invalid_arg("storage_create");
		return FAIL;
	}

	*pstore = XMALLOC(struct storage_t);
	if (!*pstore)
		return NO_MEMORY;

	(*pstore)->base_id = base_id;
	(*pstore)->max_id = max_id;
	(*pstore)->val_size = val_size;
	(*pstore)->record_size = sizeof(struct record_t) - 8 + ((val_size + 7) & ~7);

	(*pstore)->array = xcalloc(max_id - base_id, (*pstore)->record_size);
	if (!(*pstore)->array) {
		storage_destroy(pstore);
		return NO_MEMORY;
	}

	rec = (*pstore)->array;
	(*pstore)->limit = RECORD_ADDR(*pstore, rec, max_id - base_id);

	for (i = base_id; i < max_id; ++i) {
		SPIN_CREATE(&rec->lock);
		rec->id = i;
		rec = RECORD_ADDR(*pstore, rec, 1);
	}

	return OK;
}

void storage_destroy(storage_handle* pstore)
{
	if (!pstore || !*pstore)
		return;

	xfree((*pstore)->array);
	xfree(*pstore);
	*pstore = NULL;
}

int storage_get_base_id(storage_handle store)
{
	return store->base_id;
}

int storage_get_max_id(storage_handle store)
{
	return store->max_id;
}

size_t storage_get_val_size(storage_handle store)
{
	return store->val_size;
}

status storage_lookup(storage_handle store, int id, record_handle* prec)
{
	if (!prec || id < store->base_id || id >= store->max_id) {
		error_invalid_arg("storage_lookup");
		return FAIL;
	}

	*prec = RECORD_ADDR(store, store->array, id - store->base_id);
	return OK;
}

status storage_iterate(storage_handle store, storage_iterate_func iter_fn, record_handle prev, void* param)
{
	status st = TRUE;
	if (!iter_fn) {
		error_invalid_arg("storage_iterate");
		return FAIL;
	}

	if (!prev)
		prev = store->array;
	else
		prev = RECORD_ADDR(store, prev, 1);

	for (; prev < store->limit; prev = RECORD_ADDR(store, prev, 1)) {
		st = iter_fn(prev, param);
		if (FAILED(st) || !st)
			break;
	}

	return st;
}

int record_get_id(record_handle rec)
{
	return rec->id;
}

void* record_get_val(record_handle rec)
{
	return rec->val;
}

long record_get_seq(record_handle rec)
{
	return rec->seq;
}

void record_set_seq(record_handle rec, long seq)
{
	rec->seq = seq;
}
