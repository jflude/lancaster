#include "storage.h"
#include "error.h"
#include "xalloc.h"

struct record_t
{
	spin_lock_t lock;
	int id;
	long seq;
	void* confl;
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
#define RECORD_ALIGN 8

status storage_create(storage_handle* pstore, int base_id, int max_id, size_t val_size)
{
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
	(*pstore)->record_size = sizeof(struct record_t) - RECORD_ALIGN + ((val_size + RECORD_ALIGN - 1) & ~(RECORD_ALIGN - 1));

	(*pstore)->array = xmalloc((max_id - base_id) * (*pstore)->record_size);
	if (!(*pstore)->array) {
		storage_destroy(pstore);
		return NO_MEMORY;
	}

	(*pstore)->limit = RECORD_ADDR(*pstore, (*pstore)->array, max_id - base_id);
	storage_reset(*pstore);
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

void* storage_get_memory(storage_handle store)
{
	return store->array;
}

int storage_get_base_id(storage_handle store)
{
	return store->base_id;
}

int storage_get_max_id(storage_handle store)
{
	return store->max_id;
}

size_t storage_get_record_size(storage_handle store)
{
	return store->record_size;
}

size_t storage_get_val_size(storage_handle store)
{
	return store->val_size;
}

size_t storage_get_val_offset()
{
	return offsetof(struct record_t, val);
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

void storage_reset(storage_handle store)
{
	record_handle rec = store->array;
	int i;

	for (i = store->base_id; i < store->max_id; ++i) {
		SPIN_CREATE(&rec->lock);
		rec->id = i;
		rec->seq = 0;
		rec->confl = NULL;
		memset(rec->val, 0, store->val_size);

		rec = RECORD_ADDR(store, rec, 1);
	}
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

void* record_get_confl(record_handle rec)
{
	return rec->confl;
}

void record_set_confl(record_handle rec, void* confl)
{
	rec->confl = confl;
}
