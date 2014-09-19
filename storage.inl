#ifndef STORAGE_INL
#define STORAGE_INL

#include "error.h"
#include "spin.h"
#include <time.h>

struct record
{
	volatile revision rev;
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
	volatile revision last_touched_rev;
	size_t q_mask;
	q_index q_head;
	union { char reserved[1024]; } new_fields;
	struct q_element change_q[1];
};

struct storage_stats
{
	size_t q_elem_read;
	double q_min_latency;
	double q_max_latency;
	double q_mean_latency;
	double q_M2_latency;
};

struct storage
{
	struct segment* seg;
	record_handle first;
	record_handle limit;
	char* mmap_file;
	size_t mmap_size;
	int seg_fd;
	boolean is_read_only;
	boolean is_persistent;
	volatile int stats_lock;
	struct storage_stats* curr_stats;
	struct storage_stats* next_stats;
};

#define STORAGE_RECORD(stg, base, idx) \
	((record_handle) ((char*) base + (idx) * (stg)->seg->rec_size))

INLINE status storage_get_record(storage_handle store, identifier id,
	   		  					 record_handle* prec)
{
	if (!prec)
		return error_invalid_arg("storage_get_record");

	if (id < store->seg->base_id || id >= store->seg->max_id)
		return error_msg("storage_get_record: invalid identifier",
						 STORAGE_INVALID_SLOT);

	*prec = STORAGE_RECORD(store, store->first, id - store->seg->base_id);
	return OK;
}

INLINE void record_set_revision(record_handle rec, revision rev)
{
	SPIN_UNLOCK(&rec->rev, rev);
}

INLINE revision record_write_lock(record_handle rec)
{
	revision old_rev;
	SPIN_WRITE_LOCK(&rec->rev, old_rev);
	return old_rev;
}

#endif
