#include "storage.h"
#include "clock.h"
#include "error.h"
#include "spin.h"
#include "sync.h"
#include "xalloc.h"
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <time.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>

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
	volatile spin_lock stats_lock;
	struct storage_stats* curr_stats;
	struct storage_stats* next_stats;
};

#define MAGIC_NUMBER 0x0C0FFEE0
#define STORAGE_RECORD(stg, base, idx) \
	((record_handle) ((char*) base + (idx) * (stg)->seg->rec_size))

static status init_create(storage_handle* pstore, const char* mmap_file,
						  boolean persist, int open_flags, identifier base_id,
						  identifier max_id, size_t value_size,
						  size_t property_size, size_t q_capacity)
{
	size_t rec_sz, hdr_sz, seg_sz, page_sz, val_aln_sz, prop_offset;
	status st;

	BZERO(*pstore);
	(*pstore)->is_persistent = persist;

	(*pstore)->curr_stats = XMALLOC(struct storage_stats);
	if (!(*pstore)->curr_stats)
		return NO_MEMORY;

	(*pstore)->next_stats = XMALLOC(struct storage_stats);
	if (!(*pstore)->next_stats)
		return NO_MEMORY;

	val_aln_sz = ALIGNED_SIZE(value_size, DEFAULT_ALIGNMENT);
	rec_sz = offsetof(struct record, val) + val_aln_sz;

	if (property_size > 0) {
		rec_sz += ALIGNED_SIZE(property_size, DEFAULT_ALIGNMENT);
		prop_offset = offsetof(struct record, val) + val_aln_sz;
	} else
		prop_offset = 0;

	hdr_sz = offsetof(struct segment, change_q) +
		ALIGNED_SIZE(sizeof(struct q_element) *
					 (q_capacity > 0 ? q_capacity : 1), DEFAULT_ALIGNMENT);

	page_sz = sysconf(_SC_PAGESIZE);
	seg_sz =
		(hdr_sz + (rec_sz * (max_id - base_id)) + page_sz - 1) & ~(page_sz - 1);

	if (strncmp(mmap_file, "shm:", 4) == 0) {
		open_flags &= ~O_TRUNC;
	shm_loop:
		(*pstore)->seg_fd =
			shm_open(mmap_file + 4, open_flags | O_RDWR, S_IRUSR | S_IWUSR);

		if ((*pstore)->seg_fd == -1) {
			if (errno == EINTR)
				goto shm_loop;

			return error_errno("shm_open");
		}
	} else {
	open_loop:
		(*pstore)->seg_fd =
			open(mmap_file, open_flags | O_RDWR, S_IRUSR | S_IWUSR);

		if ((*pstore)->seg_fd == -1) {
			if (errno == EINTR)
				goto open_loop;

			return error_errno("open");
		}
	}

	if (open_flags & (O_CREAT | O_TRUNC)) {
	trunc_loop:
		if (ftruncate((*pstore)->seg_fd, seg_sz) == -1) {
			if (errno == EINTR)
				goto trunc_loop;

			return error_errno("ftruncate");
		}
	} else {
		struct stat file_stat;
		if (fstat((*pstore)->seg_fd, &file_stat) == -1)
			return error_errno("fstat");

		if ((size_t) file_stat.st_size != seg_sz)
			return error_msg("storage_create: storage is unequal size",
							 STORAGE_CORRUPTED);
	}

	(*pstore)->seg = mmap(NULL, seg_sz, PROT_READ | PROT_WRITE,
						  MAP_SHARED, (*pstore)->seg_fd, 0);

	if ((*pstore)->seg == MAP_FAILED) {
		(*pstore)->seg = NULL;
		return error_errno("mmap");
	}

	(*pstore)->mmap_size = seg_sz;
	(*pstore)->mmap_file = xstrdup(mmap_file);
	if (!(*pstore)->mmap_file)
		return NO_MEMORY;

	if (open_flags & (O_CREAT | O_TRUNC)) {
		(*pstore)->seg->lib_version = storage_get_current_lib_version()
		(*pstore)->seg->seg_size = seg_sz;
		(*pstore)->seg->hdr_size = hdr_sz;
		(*pstore)->seg->rec_size = rec_sz;
		(*pstore)->seg->val_size = value_size;
		(*pstore)->seg->val_offset = offsetof(struct record, val);
		(*pstore)->seg->prop_size = property_size;
		(*pstore)->seg->prop_offset = prop_offset;
		(*pstore)->seg->base_id = base_id;
		(*pstore)->seg->max_id = max_id;
		(*pstore)->seg->q_mask = q_capacity - 1;

		BZERO((*pstore)->seg->description);
	} else if (((*pstore)->seg->lib_version >> 8) != STORAGE_MAJOR_VERSION)
		return error_msg("storage_create: incompatible library version",
						 STORAGE_WRONG_VERSION);
	else if ((*pstore)->seg->seg_size != seg_sz ||
			 (*pstore)->seg->hdr_size != hdr_sz ||
			 (*pstore)->seg->rec_size != rec_sz ||
			 (*pstore)->seg->val_size != value_size ||
			 (*pstore)->seg->val_offset != offsetof(struct record, val) ||
			 (*pstore)->seg->prop_size != property_size ||
			 (*pstore)->seg->prop_offset != prop_offset ||
			 (*pstore)->seg->q_mask != (q_capacity - 1))
		return error_msg("storage_create: storage is unequal structure",
						 STORAGE_CORRUPTED);

	(*pstore)->first = (void*) (((char*) (*pstore)->seg) + hdr_sz);
	(*pstore)->limit =
		STORAGE_RECORD(*pstore, (*pstore)->first, max_id - base_id);

	if ((open_flags & (O_CREAT | O_EXCL)) != (O_CREAT | O_EXCL)) {
		record_handle r;
		for (r = (*pstore)->first;
			 r < (*pstore)->limit;
			 r = STORAGE_RECORD(*pstore, r, 1))
			if (r->rev < 0)
				r->rev &= ~SPIN_MASK;

		SYNC_SYNCHRONIZE();
	}

	if (FAILED(st = clock_time(&(*pstore)->seg->last_created)))
		return st;

	if (open_flags & (O_CREAT | O_TRUNC))
		(*pstore)->seg->magic = MAGIC_NUMBER;

	return st;
}

static status init_open(storage_handle* pstore, const char* mmap_file,
						int open_flags)
{
	int mmap_flags = PROT_READ;
	size_t seg_sz;

	BZERO(*pstore);
	(*pstore)->is_persistent = TRUE;

	(*pstore)->curr_stats = XMALLOC(struct storage_stats);
	if (!(*pstore)->curr_stats)
		return NO_MEMORY;

	(*pstore)->next_stats = XMALLOC(struct storage_stats);
	if (!(*pstore)->next_stats)
		return NO_MEMORY;

	if (open_flags == O_RDONLY)
		(*pstore)->is_read_only = TRUE;
	else
		mmap_flags |= PROT_WRITE;

	if (strncmp(mmap_file, "shm:", 4) == 0) {
	shm_loop:
		(*pstore)->seg_fd = shm_open(mmap_file + 4, open_flags, 0);

		if ((*pstore)->seg_fd == -1) {
			if (errno == EINTR)
				goto shm_loop;

			return error_errno("shm_open");
		}
	} else {
		struct stat file_stat;
	open_loop:
		(*pstore)->seg_fd = open(mmap_file, open_flags);

		if ((*pstore)->seg_fd == -1) {
			if (errno == EINTR)
				goto open_loop;

			return error_errno("open");
		}

		if (fstat((*pstore)->seg_fd, &file_stat) == -1)
			return error_errno("fstat");

		if ((size_t) file_stat.st_size < sizeof(struct segment))
			return error_msg("storage_open: storage is truncated",
							 STORAGE_CORRUPTED);
	}

	(*pstore)->seg = mmap(NULL, sizeof(struct segment), PROT_READ,
						  MAP_SHARED, (*pstore)->seg_fd, 0);

	if ((*pstore)->seg == MAP_FAILED) {
		(*pstore)->seg = NULL;
		return error_errno("mmap");
	}

	(*pstore)->mmap_size = sizeof(struct segment);

	if ((*pstore)->seg->magic != MAGIC_NUMBER)
		return error_msg("storage_open: storage is corrupt", STORAGE_CORRUPTED);

	if (((*pstore)->seg->lib_version >> 8) != STORAGE_MAJOR_VERSION)
		return error_msg("storage_create: incompatible library version",
						 STORAGE_WRONG_VERSION);

	seg_sz = (*pstore)->seg->seg_size;

	if (munmap((*pstore)->seg, (*pstore)->mmap_size) == -1)
		return error_errno("munmap");

	(*pstore)->seg =
		mmap(NULL, seg_sz, mmap_flags, MAP_SHARED, (*pstore)->seg_fd, 0);

	if ((*pstore)->seg == MAP_FAILED) {
		(*pstore)->seg = NULL;
		return error_errno("mmap");
	}

	(*pstore)->mmap_size = seg_sz;
	(*pstore)->mmap_file = xstrdup(mmap_file);
	if (!(*pstore)->mmap_file)
		return NO_MEMORY;

	(*pstore)->first = 
		(void*) (((char*) (*pstore)->seg) + (*pstore)->seg->hdr_size);

	(*pstore)->limit = 
		STORAGE_RECORD(*pstore, (*pstore)->first,
					   (*pstore)->seg->max_id - (*pstore)->seg->base_id);
	return OK;
}

status storage_create(storage_handle* pstore, const char* mmap_file,
					  boolean persist, int open_flags, identifier base_id,
					  identifier max_id, size_t value_size,
					  size_t property_size, size_t q_capacity)
{
	/* q_capacity must be a power of 2 */
	status st;
	if (!pstore || !mmap_file ||
		open_flags & ~(O_CREAT | O_EXCL | O_TRUNC) ||
		max_id <= base_id || value_size == 0 ||
		q_capacity == 1 || (q_capacity & (q_capacity - 1)) != 0)
		return error_invalid_arg("storage_create");

	*pstore = XMALLOC(struct storage);
	if (!*pstore)
		return NO_MEMORY;

	if (FAILED(st = init_create(pstore, mmap_file, persist, open_flags,
								base_id, max_id, value_size, property_size,
								q_capacity))) {
		error_save_last();
		storage_destroy(pstore);
		error_restore_last();
	}

	return st;
}

status storage_open(storage_handle* pstore, const char* mmap_file,
					int open_flags)
{
	status st;
	if (!pstore || !mmap_file ||
		(open_flags != O_RDONLY && open_flags != O_RDWR))
		return error_invalid_arg("storage_open");

	*pstore = XMALLOC(struct storage);
	if (!*pstore)
		return NO_MEMORY;

	if (FAILED(st = init_open(pstore, mmap_file, open_flags))) {
		error_save_last();
		storage_destroy(pstore);
		error_restore_last();
	}

	return st;
}

status storage_destroy(storage_handle* pstore)
{
	status st = OK;
	if (!pstore || !*pstore)
		return st;

loop:
	if ((*pstore)->seg_fd != -1 && close((*pstore)->seg_fd) == -1) {
		if (errno == EINTR)
			goto loop;

		return error_errno("close");
	}

	if ((*pstore)->seg) {
		if (munmap((*pstore)->seg, (*pstore)->mmap_size) == -1)
			return error_errno("munmap");

		if (!(*pstore)->is_persistent) {
			if (strncmp((*pstore)->mmap_file, "shm:", 4) == 0) {
				if (shm_unlink((*pstore)->mmap_file + 4) == -1)
					return error_errno("shm_unlink");
			} else {
				if (unlink((*pstore)->mmap_file) == -1)
					return error_errno("unlink");
			}
		}
	}

	XFREE((*pstore)->mmap_file);
	XFREE(*pstore);
	return st;
}

boolean storage_is_read_only(storage_handle store)
{
	return store->is_read_only;
}

status storage_set_persistence(storage_handle store, boolean persist)
{
	boolean old_val;
	if (store->is_read_only)
		return error_msg("storage_set_persistence: storage is read-only",
						 STORAGE_READ_ONLY);

	old_val = store->is_persistent;
	store->is_persistent = persist;
	return old_val;
}

unsigned short storage_get_current_lib_version() 
{
	return (STORAGE_MAJOR_VERSION << 8) | STORAGE_MINOR_VERSION;
}

unsigned short storage_get_lib_version(storage_handle store)
{
	return store->seg->lib_version;
}

unsigned short storage_get_app_version(storage_handle store)
{
	return store->seg->app_version;
}

status storage_set_app_version(storage_handle store, unsigned short app_ver)
{
	if (store->is_read_only)
		return error_msg("storage_set_app_version: storage is read-only",
						 STORAGE_READ_ONLY);

	store->seg->app_version = app_ver;
	return OK;
}

record_handle storage_get_array(storage_handle store)
{
	return store->first;
}

identifier storage_get_base_id(storage_handle store)
{
	return store->seg->base_id;
}

identifier storage_get_max_id(storage_handle store)
{
	return store->seg->max_id;
}

size_t storage_get_record_size(storage_handle store)
{
	return store->seg->rec_size;
}

size_t storage_get_property_size(storage_handle store)
{
	return store->seg->prop_size;
}

size_t storage_get_value_size(storage_handle store)
{
	return store->seg->val_size;
}

size_t storage_get_value_offset(storage_handle store)
{
	return store->seg->val_offset;
}

size_t storage_get_property_offset(storage_handle store)
{
	return store->seg->prop_offset;
}

const char* storage_get_file(storage_handle store)
{
	return store->mmap_file;
}

const char* storage_get_description(storage_handle store)
{
	return store->seg->description;
}

status storage_set_description(storage_handle store, const char* desc)
{
	if (!desc)
		return error_invalid_arg("storage_set_description");

	if (store->is_read_only)
		return error_msg("storage_set_description: storage is read-only",
						 STORAGE_READ_ONLY);

	if (strlen(desc) >= sizeof(store->seg->description))
		return error_msg("storage_set_description: description too long",
						 BUFFER_TOO_SMALL);

	strcpy(store->seg->description, desc);
	return OK;
}

status storage_get_created_time(storage_handle store, microsec* when)
{
	if (!when)
		return error_invalid_arg("storage_get_created_time");

	*when = store->seg->last_created;
	return OK;
}

status storage_get_touched_time(storage_handle store, microsec* when)
{
	status st;
	revision rev;
	microsec t;

	if (!when)
		return error_invalid_arg("storage_get_touched_time");

	do {
		if (FAILED(st = spin_read_lock(&store->seg->last_touched_rev, &rev)))
			return st;

		t = store->seg->last_touched;
	} while (rev != store->seg->last_touched_rev);

	*when = t;
	return OK;
}

status storage_touch(storage_handle store, microsec when)
{
	status st;
	revision rev;

	if (store->is_read_only)
		return error_msg("storage_touch: storage is read-only",
						 STORAGE_READ_ONLY);

	if (FAILED(st = spin_write_lock(&store->seg->last_touched_rev, &rev)))
		return st;

	store->seg->last_touched = when;
	spin_unlock(&store->seg->last_touched_rev, NEXT_REV(rev));
	return OK;
}

const struct q_element* storage_get_queue_base_ref(storage_handle store)
{
	return store->seg->change_q;
}

const q_index* storage_get_queue_head_ref(storage_handle store)
{
	return &store->seg->q_head;
}

size_t storage_get_queue_capacity(storage_handle store)
{
	return store->seg->q_mask + 1;
}

q_index storage_get_queue_head(storage_handle store)
{
	return store->seg->q_head;
}

status storage_write_queue(storage_handle store, identifier id)
{
	struct q_element* q;
	if (store->is_read_only)
		return error_msg("storage_write_queue: storage is read-only",
						 STORAGE_READ_ONLY);

	if (store->seg->q_mask == -1u)
		return error_msg("storage_write_queue: no change queue",
						 STORAGE_NO_CHANGE_QUEUE);

	q = &store->seg->change_q[store->seg->q_head++ & store->seg->q_mask];
	q->id = id;
	return clock_time(&q->ts);
}

status storage_read_queue(storage_handle store, q_index idx,
						  struct q_element* pelem, boolean update_stats)
{
	double latency, delta;
	microsec now;
	status st;

	if (!pelem)
		return error_invalid_arg("storage_read_queue");

	if (store->seg->q_mask == -1u)
		return error_msg("storage_read_queue: no change queue",
						 STORAGE_NO_CHANGE_QUEUE);

	*pelem = store->seg->change_q[idx & store->seg->q_mask];
	if (!update_stats)
		return OK;

	if (FAILED(st = clock_time(&now)))
		return st;

	latency = now - pelem->ts;

	if (FAILED(st = spin_write_lock(&store->stats_lock, NULL)))
		return st;

	delta = latency - store->next_stats->q_mean_latency;

	store->next_stats->q_mean_latency +=
		delta / ++store->next_stats->q_elem_read;

	store->next_stats->q_M2_latency +=
		delta * (latency - store->next_stats->q_mean_latency);

	if (store->next_stats->q_min_latency == 0 ||
		latency < store->next_stats->q_min_latency)
		store->next_stats->q_min_latency = latency;

	if (store->next_stats->q_max_latency == 0 ||
		latency > store->next_stats->q_max_latency)
		store->next_stats->q_max_latency = latency;

	spin_unlock(&store->stats_lock, 0);
	return OK;
}

status storage_get_id(storage_handle store, record_handle rec,
					  identifier* pident)
{
	if (!pident)
		return error_invalid_arg("storage_get_id");

	if (rec < store->first || rec >= store->limit)
		return error_msg("storage_get_id: invalid record address",
						 STORAGE_INVALID_SLOT);

	*pident = ((char*) rec - (char*) store->first) / store->seg->rec_size;
	return OK;
}

status storage_get_record(storage_handle store, identifier id,
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

status storage_iterate(storage_handle store, storage_iterate_func iter_fn,
					   record_handle prev, void* param)
{
	status st = TRUE;
	if (!iter_fn)
		return error_invalid_arg("storage_iterate");

	for (prev = (prev ? STORAGE_RECORD(store, prev, 1) : store->first);
		 prev < store->limit; 
		 prev = STORAGE_RECORD(store, prev, 1))
		if (FAILED(st = iter_fn(store, prev, param)) || !st)
			break;

	return st;
}

status storage_sync(storage_handle store)
{
	if (store->is_read_only)
		return error_msg("storage_sync: storage is read-only",
						 STORAGE_READ_ONLY);

	if (store->seg->seg_size > 0 &&
		msync(store->seg, store->seg->seg_size, MS_SYNC) == -1)
		return error_errno("msync");

	return OK;
}

status storage_reset(storage_handle store)
{
	if (store->is_read_only)
		return error_msg("storage_reset: storage is read-only",
						 STORAGE_READ_ONLY);

	memset(store->first, 0, (char*) store->limit - (char*) store->first);

	store->seg->q_head = 0;
	if (store->seg->q_mask != -1u)
		memset(store->seg->change_q, 0,
			   (store->seg->q_mask + 1) * sizeof(struct q_element));

	BZERO(&store->curr_stats);
	BZERO(&store->next_stats);

	SYNC_SYNCHRONIZE();
	return OK;
}

status storage_grow(storage_handle store, storage_handle* pnewstore,
					const char* new_mmap_file, identifier new_base_id,
					identifier new_max_id, size_t new_value_size,
					size_t new_property_size, size_t new_q_capacity)
{
	status st;
	size_t copy_sz;
	record_handle old_r, new_r;

	if (!pnewstore || !new_mmap_file ||
		strcmp(new_mmap_file, store->mmap_file) == 0)
		return error_invalid_arg("storage_grow");

	if (FAILED(st = storage_create(pnewstore, new_mmap_file, FALSE,
								   O_CREAT | O_TRUNC, new_base_id, new_max_id,
								   new_value_size, new_property_size,
								   new_q_capacity)))
		return st;

	copy_sz = sizeof(revision) +
		(store->seg->val_size < (*pnewstore)->seg->val_size
		 ? store->seg->val_size : (*pnewstore)->seg->val_size);

	for (old_r = store->first, new_r = (*pnewstore)->first;
		 old_r < store->limit && new_r < (*pnewstore)->limit;
		 old_r = STORAGE_RECORD(store, old_r, 1),
			 new_r = STORAGE_RECORD(*pnewstore, new_r, 1))
		memcpy(new_r, old_r, copy_sz);

	SYNC_SYNCHRONIZE();

	if (new_property_size > 0) {
		copy_sz = (store->seg->prop_size < (*pnewstore)->seg->prop_size
				   ? store->seg->prop_size : (*pnewstore)->seg->prop_size);

		for (old_r = store->first, new_r = (*pnewstore)->first;
			 old_r < store->limit && new_r < (*pnewstore)->limit;
			 old_r = STORAGE_RECORD(store, old_r, 1),
				 new_r = STORAGE_RECORD(*pnewstore, new_r, 1))
			memcpy((char*) new_r + (*pnewstore)->seg->prop_offset,
				   (char*) old_r + store->seg->prop_offset, copy_sz);
	}

	(*pnewstore)->seg->app_version = store->seg->app_version;
	strcpy((*pnewstore)->seg->description, store->seg->description);

	if (FAILED(st = clock_time(&(*pnewstore)->seg->last_created))) {
		error_save_last();
		storage_destroy(pnewstore);
		error_restore_last();
		return st;
	}

	(*pnewstore)->is_persistent = store->is_persistent;
	return OK;
}

void* storage_get_property_ref(storage_handle store, record_handle rec)
{
	return store->seg->prop_offset
		? ((char*) rec + store->seg->prop_offset) : NULL;
}

void* record_get_value_ref(record_handle rec)
{
	return rec->val;
}

revision record_get_revision(record_handle rec)
{
	return rec->rev;
}

void record_set_revision(record_handle rec, revision rev)
{
	spin_unlock(&rec->rev, rev);
}

status record_read_lock(record_handle rec, revision* old_rev)
{
	return spin_read_lock(&rec->rev, old_rev);
}

status record_write_lock(record_handle rec, revision* old_rev)
{
	return spin_write_lock(&rec->rev, old_rev);
}

double storage_get_queue_min_latency(storage_handle store)
{
	return store->curr_stats->q_min_latency;
}

double storage_get_queue_max_latency(storage_handle store)
{
	return store->curr_stats->q_max_latency;
}

double storage_get_queue_mean_latency(storage_handle store)
{
	return store->curr_stats->q_mean_latency;
}

double storage_get_queue_stddev_latency(storage_handle store)
{
	return (store->curr_stats->q_elem_read > 1
			? sqrt(store->curr_stats->q_M2_latency /
				   (store->curr_stats->q_elem_read - 1))
			: 0);
}

status storage_next_stats(storage_handle store)
{
	status st;
	struct storage_stats* tmp;

	if (FAILED(st = spin_write_lock(&store->stats_lock, NULL)))
		return st;

	tmp = store->next_stats;
	store->next_stats = store->curr_stats;
	store->curr_stats = tmp;

	BZERO(store->next_stats);

	spin_unlock(&store->stats_lock, 0);
	return st;
}
