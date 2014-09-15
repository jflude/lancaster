#ifndef STORAGE_INL
#define STORAGE_INL

#include "error.h"
#include "spin.h"

struct record
{
	volatile version ver;
	char val[1];
};

struct segment
{
	unsigned magic;
	unsigned short lib_version;
	unsigned short app_version;
	char description[256];
	size_t seg_size;
	size_t hdr_size;
	size_t rec_size;
	size_t val_size;
	size_t val_offset;
	size_t prop_size;
	size_t prop_offset;
	identifier base_id;
	identifier max_id;
	microsec last_created;
	microsec last_touched;
	volatile version last_touched_ver;
	size_t q_mask;
	queue_index q_head;
	union { char reserved[1024]; } new_fields;
	identifier change_q[1];
};

struct storage
{
	struct segment* seg;
	record_handle first;
	record_handle limit;
	char* mmap_file;
	size_t mmap_size;
	boolean is_read_only;
	boolean is_persistent;
	int fd;
};

#define RECORD_ADDRESS(stg, base, idx) \
	((record_handle) ((char*) base + (idx) * (stg)->seg->rec_size))

INLINE status storage_get_record(storage_handle store, identifier id,
	   		  					 record_handle* prec)
{
	if (!prec)
		return error_invalid_arg("storage_get_record");

	if (id < store->seg->base_id || id >= store->seg->max_id)
		return error_msg("storage_get_record: invalid identifier",
						 STORAGE_INVALID_SLOT);

	*prec = RECORD_ADDRESS(store, store->first, id - store->seg->base_id);
	return OK;
}

INLINE identifier storage_read_queue(storage_handle store, queue_index index)
{
	return store->seg->change_q[index & store->seg->q_mask];
}

INLINE void record_set_version(record_handle rec, version ver)
{
	SPIN_UNLOCK(&rec->ver, ver);
}

INLINE version record_write_lock(record_handle rec)
{
	version old_ver;
	SPIN_WRITE_LOCK(&rec->ver, old_ver);
	return old_ver;
}

#endif
