/*
  Copyright (c)2014-2018 Peak6 Investments, LP.  All rights reserved.
  Use of this source code is governed by the COPYING file.
*/

#include <lancaster/clock.h>
#include <lancaster/error.h>
#include <lancaster/spin.h>
#include <lancaster/storage.h>
#include <lancaster/sync.h>
#include <lancaster/version.h>
#include <lancaster/xalloc.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include "xalloca.h"

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifndef O_ACCMODE
#define O_ACCMODE (O_RDONLY | O_WRONLY | O_RDWR)
#endif

#ifndef O_NOFOLLOW
#define O_NOFOLLOW 0
#endif

#ifndef _SC_PAGESIZE
#define _SC_PAGESIZE _SC_PAGE_SIZE
#endif

struct record {
    volatile revision rev;
    microsec ts;
    char val[1];
};

struct segment {
    unsigned magic;
    unsigned short file_version;
    unsigned short data_version;
    char description[256];
    size_t seg_size;
    size_t hdr_size;
    size_t rec_size;
    size_t val_size;
    size_t prop_size;
    size_t ts_offset;
    size_t val_offset;
    size_t prop_offset;
    identifier base_id;
    identifier max_id;
    microsec last_created;
    microsec last_touched;
    volatile revision last_touched_rev;
    size_t q_mask;
    q_index q_head;
    union {
	char reserved[1024];
    } new_fields;
    identifier change_q[1];
};

struct storage {
    struct segment *seg;
    record_handle first;
    record_handle limit;
    char *mmap_file;
    size_t mmap_size;
    int seg_fd;
    boolean is_read_only;
    boolean is_persistent;
};

#define MAGIC_NUMBER 0x0C0FFEE0

#define STORAGE_RECORD(stg, base, idx)					\
    ((record_handle)((char *)base + (idx) * (stg)->seg->rec_size))

static status init_create(storage_handle *pstore, const char *mmap_file,
			  int open_flags, mode_t mode_flags, boolean persist,
			  identifier base_id, identifier max_id,
			  size_t value_size, size_t property_size,
			  size_t q_capacity, const char *desc)
{
    status st;
    size_t rec_sz, hdr_sz, seg_sz, page_sz, prop_offset;

    BZERO(*pstore);
    (*pstore)->seg_fd = -1;
    (*pstore)->is_persistent = persist;

    rec_sz = offsetof(struct record, val) +
	ALIGNED_SIZE(value_size, DEFAULT_ALIGNMENT);

    if (property_size > 0) {
	prop_offset = rec_sz;
	rec_sz += ALIGNED_SIZE(property_size, DEFAULT_ALIGNMENT);
    } else
	prop_offset = 0;

    hdr_sz = offsetof(struct segment, change_q) +
	ALIGNED_SIZE(sizeof(identifier) *
		     (q_capacity > 0 ? q_capacity : 1), DEFAULT_ALIGNMENT);

    page_sz = sysconf(_SC_PAGESIZE);
    seg_sz =
	(hdr_sz + (rec_sz * (max_id - base_id)) + page_sz - 1) & ~(page_sz - 1);

    if (strncmp(mmap_file, "shm:", 4) == 0) {
	(*pstore)->seg_fd = shm_open(mmap_file + 4, open_flags, mode_flags);
	if ((*pstore)->seg_fd == -1)
	    return error_eintr("shm_open");
    } else {
	(*pstore)->seg_fd = open(mmap_file, open_flags, mode_flags);
	if ((*pstore)->seg_fd == -1)
	    return error_eintr("open");
    }

    if (open_flags & O_CREAT) {
	if (ftruncate((*pstore)->seg_fd, seg_sz) == -1) {
	    if (errno == EINTR)
		return error_eintr("ftruncate");
/* Darwin does not support calling ftruncate on an existing segment */
#ifdef DARWIN_OS
	    if (errno != EINVAL)
#endif
		return error_errno("ftruncate");
	}
    } else {
	struct stat file_stat;
	if (fstat((*pstore)->seg_fd, &file_stat) == -1)
	    return error_errno("fstat");

	if ((size_t)file_stat.st_size != seg_sz)
	    return error_msg(STORAGE_UNEQUAL,
			     "storage_create: storage is unequal");
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

    if (open_flags & O_CREAT) {
	(*pstore)->seg->file_version =
	    (version_get_file_major() << 8) | version_get_file_minor();

	(*pstore)->seg->seg_size = seg_sz;
	(*pstore)->seg->hdr_size = hdr_sz;
	(*pstore)->seg->rec_size = rec_sz;
	(*pstore)->seg->val_size = value_size;
	(*pstore)->seg->prop_size = property_size;
	(*pstore)->seg->ts_offset = offsetof(struct record, ts);
	(*pstore)->seg->val_offset = offsetof(struct record, val);
	(*pstore)->seg->prop_offset = prop_offset;
	(*pstore)->seg->base_id = base_id;
	(*pstore)->seg->max_id = max_id;
	(*pstore)->seg->q_mask = q_capacity - 1;

	if (FAILED(st = storage_set_description(*pstore, desc)))
	    return st;
    } else if (((*pstore)->seg->file_version >> 8) !=
	       version_get_file_major())
	return error_msg(WRONG_FILE_VERSION,
			 "storage_create: incompatible file version");
    else if ((*pstore)->seg->seg_size != seg_sz ||
	     (*pstore)->seg->hdr_size != hdr_sz ||
	     (*pstore)->seg->rec_size != rec_sz ||
	     (*pstore)->seg->val_size != value_size ||
	     (*pstore)->seg->prop_size != property_size ||
	     (*pstore)->seg->ts_offset != offsetof(struct record, ts) ||
	     (*pstore)->seg->val_offset != offsetof(struct record, val) ||
	     (*pstore)->seg->prop_offset != prop_offset ||
	     (*pstore)->seg->q_mask != (q_capacity - 1) ||
	     (!desc && (*pstore)->seg->description[0] != '\0') ||
	     (desc && strcmp(desc, (*pstore)->seg->description) != 0))
	return error_msg(STORAGE_UNEQUAL,
			 "storage_create: storage is unequal");

    (*pstore)->first = (void *)(((char *)(*pstore)->seg) + hdr_sz);
    (*pstore)->limit =
	STORAGE_RECORD(*pstore, (*pstore)->first, max_id - base_id);

    if ((open_flags & (O_CREAT | O_EXCL)) != (O_CREAT | O_EXCL)) {
	record_handle r;
	for (r = (*pstore)->first;
	     r < (*pstore)->limit; r = STORAGE_RECORD(*pstore, r, 1))
	    if (r->rev < 0)
		r->rev &= ~SPIN_MASK;

	SYNC_SYNCHRONIZE();
    }

    if (FAILED(st = clock_time(&(*pstore)->seg->last_created)))
	return st;

    if (open_flags & O_CREAT)
	(*pstore)->seg->magic = MAGIC_NUMBER;

    SYNC_SYNCHRONIZE();
    return storage_sync(*pstore);
}

static status init_open(storage_handle *pstore, const char *mmap_file,
			int open_flags)
{
    size_t seg_sz;
    struct stat file_stat;
    int mmap_flags = PROT_READ;

    BZERO(*pstore);
    (*pstore)->seg_fd = -1;
    (*pstore)->is_persistent = TRUE;

    if ((open_flags & O_ACCMODE) == O_RDONLY)
	(*pstore)->is_read_only = TRUE;
    else
	mmap_flags |= PROT_WRITE;

    if (strncmp(mmap_file, "shm:", 4) == 0) {
	(*pstore)->seg_fd = shm_open(mmap_file + 4, open_flags, 0);
	if ((*pstore)->seg_fd == -1)
	    return error_eintr("shm_open");
    } else {
	(*pstore)->seg_fd = open(mmap_file, open_flags);
	if ((*pstore)->seg_fd == -1)
	    return error_eintr("open");
    }

    if (fstat((*pstore)->seg_fd, &file_stat) == -1)
	return error_errno("fstat");

    if ((size_t)file_stat.st_size < sizeof(struct segment))
	return error_msg(STORAGE_CORRUPTED,
			 "storage_open: storage is truncated");

    (*pstore)->seg = mmap(NULL, sizeof(struct segment), PROT_READ,
			  MAP_SHARED, (*pstore)->seg_fd, 0);

    if ((*pstore)->seg == MAP_FAILED) {
	(*pstore)->seg = NULL;
	return error_errno("mmap");
    }

    (*pstore)->mmap_size = sizeof(struct segment);

    if ((*pstore)->seg->magic != MAGIC_NUMBER)
	return error_msg(STORAGE_CORRUPTED, "storage_open: storage is corrupt");

    if (((*pstore)->seg->file_version >> 8) != version_get_file_major())
	return error_msg(WRONG_FILE_VERSION,
			 "storage_open: incompatible file version");

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
	(void *)(((char *)(*pstore)->seg) + (*pstore)->seg->hdr_size);

    (*pstore)->limit =
	STORAGE_RECORD(*pstore, (*pstore)->first,
		       (*pstore)->seg->max_id - (*pstore)->seg->base_id);
    return OK;
}

status storage_create(storage_handle *pstore, const char *mmap_file,
		      int open_flags, mode_t mode_flags, boolean persist,
		      identifier base_id, identifier max_id,
		      size_t value_size, size_t property_size,
		      size_t q_capacity, const char *desc)
{
    status st;
    if (!pstore || !mmap_file || max_id <= base_id || value_size == 0)
	return error_invalid_arg("storage_create");

    /* NB. q_capacity must be zero or a non-zero power of 2 */
    if (q_capacity == 1 || (q_capacity & (q_capacity - 1)) != 0)
	return error_msg(INVALID_CAPACITY,
			 "storage_create: invalid queue capacity");

    if ((open_flags & O_ACCMODE) != O_RDWR ||
	open_flags & ~(O_RDWR | O_CREAT | O_EXCL | O_NOFOLLOW))
	return error_msg(INVALID_OPEN_FLAGS,
			 "storage_create: invalid open flags: 0%07o",
			 (unsigned)open_flags);

    *pstore = XMALLOC(struct storage);
    if (!*pstore)
	return NO_MEMORY;

    if (FAILED(st = init_create(pstore, mmap_file, open_flags, mode_flags,
				persist, base_id, max_id, value_size,
				property_size, q_capacity, desc))) {
	error_save_last();
	storage_destroy(pstore);
	error_restore_last();
    }

    return st;
}

status storage_open(storage_handle *pstore, const char *mmap_file,
		    int open_flags)
{
    status st;
    if (!pstore || !mmap_file)
	return error_invalid_arg("storage_open");

    if (((open_flags & O_ACCMODE) != O_RDONLY &&
	 (open_flags & O_ACCMODE) != O_RDWR) ||
	(open_flags & ~(O_RDONLY | O_RDWR | O_NOFOLLOW)))
	return error_msg(INVALID_OPEN_FLAGS,
			 "storage_open: invalid open flags: 0%07o",
			 (unsigned)open_flags);

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

status storage_destroy(storage_handle *pstore)
{
    status st = OK;
    if (!pstore || !*pstore)
	return st;

    if ((*pstore)->seg_fd != -1) {
	if (close((*pstore)->seg_fd) == -1)
	    return error_eintr("close");

	(*pstore)->seg_fd = -1;
    }

    if ((*pstore)->seg) {
	if (munmap((*pstore)->seg, (*pstore)->mmap_size) == -1)
	    return error_errno("munmap");

	(*pstore)->seg = NULL;

	if (!(*pstore)->is_persistent &&
	    FAILED(st = storage_delete((*pstore)->mmap_file, FALSE)))
	    return st;
    }

    xfree((*pstore)->mmap_file);
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
	return error_msg(STORAGE_READ_ONLY,
			 "storage_set_persistence: storage is read-only");

    old_val = store->is_persistent;
    store->is_persistent = persist;
    return old_val;
}

unsigned short storage_get_file_version(storage_handle store)
{
    return store->seg->file_version;
}

unsigned short storage_get_data_version(storage_handle store)
{
    return store->seg->data_version;
}

status storage_set_data_version(storage_handle store, unsigned short data_ver)
{
    if (store->is_read_only)
	return error_msg(STORAGE_READ_ONLY,
			 "storage_set_app_version: storage is read-only");

    store->seg->data_version = data_ver;
    return OK;
}

const void *storage_get_segment(storage_handle store)
{
    return store->seg;
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

size_t storage_get_segment_size(storage_handle store)
{
    return store->seg->seg_size;
}

size_t storage_get_record_size(storage_handle store)
{
    return store->seg->rec_size;
}

size_t storage_get_value_size(storage_handle store)
{
    return store->seg->val_size;
}

size_t storage_get_property_size(storage_handle store)
{
    return store->seg->prop_size;
}

size_t storage_get_value_offset(storage_handle store)
{
    return store->seg->val_offset;
}

size_t storage_get_property_offset(storage_handle store)
{
    return store->seg->prop_offset;
}

size_t storage_get_timestamp_offset(storage_handle store)
{
    return store->seg->ts_offset;
}

const char *storage_get_file(storage_handle store)
{
    return store->mmap_file;
}

const char *storage_get_description(storage_handle store)
{
    return store->seg->description;
}

status storage_set_description(storage_handle store, const char *desc)
{
    if (store->is_read_only)
	return error_msg(STORAGE_READ_ONLY,
			 "storage_set_description: storage is read-only");

    if (desc) {
	if (strlen(desc) >= sizeof(store->seg->description))
	    return error_msg(BUFFER_TOO_SMALL,
			     "storage_set_description: description too long");

	strcpy(store->seg->description, desc);
    } else
	memset(store->seg->description, 0, sizeof(store->seg->description));

    return OK;
}

status storage_get_created_time(storage_handle store, microsec *when)
{
    if (!when)
	return error_invalid_arg("storage_get_created_time");

    *when = store->seg->last_created;
    return OK;
}

status storage_get_touched_time(storage_handle store, microsec *when)
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
	return error_msg(STORAGE_READ_ONLY,
			 "storage_touch: storage is read-only");

    if (FAILED(st = spin_write_lock(&store->seg->last_touched_rev, &rev)))
	return st;

    store->seg->last_touched = when;
    spin_unlock(&store->seg->last_touched_rev, NEXT_REV(rev));
    return OK;
}

const identifier *storage_get_queue_base_ref(storage_handle store)
{
    return store->seg->change_q;
}

const q_index *storage_get_queue_head_ref(storage_handle store)
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
    if (store->is_read_only)
	return error_msg(STORAGE_READ_ONLY,
			 "storage_write_queue: storage is read-only");

    if (store->seg->q_mask == (size_t) - 1)
	return error_msg(NO_CHANGE_QUEUE,
			 "storage_write_queue: no change queue");

    store->seg->change_q[store->seg->q_head & store->seg->q_mask] = id;
    SYNC_SYNCHRONIZE();
    ++store->seg->q_head;
    return OK;
}

status storage_read_queue(storage_handle store, q_index idx,
			  identifier *pident)
{
    if (!pident)
	return error_invalid_arg("storage_read_queue");

    if (store->seg->q_mask == (size_t) - 1)
	return error_msg(NO_CHANGE_QUEUE,
			 "storage_read_queue: no change queue");

    *pident = store->seg->change_q[idx & store->seg->q_mask];
    return OK;
}

status storage_get_id(storage_handle store, record_handle rec,
		      identifier *pident)
{
    if (!pident)
	return error_invalid_arg("storage_get_id");

    if (rec < store->first || rec >= store->limit)
	return error_msg(INVALID_RECORD,
			 "storage_get_id: invalid record address");

    *pident = ((char *)rec - (char *)store->first) / store->seg->rec_size;
    return OK;
}

status storage_get_record(storage_handle store, identifier id,
			  record_handle *prec)
{
    if (!prec)
	return error_invalid_arg("storage_get_record");

    if (id < store->seg->base_id || id >= store->seg->max_id)
	return error_msg(INVALID_RECORD,
			 "storage_get_record: invalid identifier");

    *prec = STORAGE_RECORD(store, store->first, id - store->seg->base_id);
    return OK;
}

status storage_find_next_unused(storage_handle store, record_handle prior,
				record_handle *prec, revision *old_rev)
{
    if (!prec)
	return error_invalid_arg("storage_find_next_unused");

    if (old_rev && store->is_read_only)
	return error_msg(STORAGE_READ_ONLY,
			 "storage_find_next_unused: storage is read-only");

    if (prior) {
	if (prior < store->first || prior >= store->limit)
	    return error_invalid_arg("storage_find_next_unused");

	prior = STORAGE_RECORD(store, prior, 1);
    } else
	prior = store->first;

    for (; prior < store->limit; prior = STORAGE_RECORD(store, prior, 1)) {
	revision rev;
	if (old_rev) {
	    status st;
	    if (FAILED(st = spin_write_lock(&prior->rev, &rev)))
		return st;
	} else
	    rev = prior->rev;

	if (rev == 0) {
	    *prec = prior;
	    if (old_rev)
		*old_rev = rev;

	    return TRUE;
	}

	if (old_rev)
	    spin_unlock(&prior->rev, rev);
    }

    return FALSE;
}

status storage_find_prev_used(storage_handle store, record_handle prior,
			      record_handle *prec, revision *old_rev)
{
    if (!prec)
	return error_invalid_arg("storage_find_prev_used");

    if (old_rev && store->is_read_only)
	return error_msg(STORAGE_READ_ONLY,
			 "storage_find_prev_used: storage is read-only");

    if (prior) {
	if (prior < store->first || prior >= store->limit)
	    return error_invalid_arg("storage_find_prev_unused");

	prior = STORAGE_RECORD(store, prior, -1);
    } else
	prior = STORAGE_RECORD(store, store->limit, -1);

    for (; prior >= store->first; prior = STORAGE_RECORD(store, prior, -1)) {
	revision rev;
	if (old_rev) {
	    status st;
	    if (FAILED(st = spin_write_lock(&prior->rev, &rev)))
		return st;
	} else
	    rev = prior->rev;

	if (rev != 0) {
	    *prec = prior;
	    if (old_rev)
		*old_rev = rev;

	    return TRUE;
	}

	if (old_rev)
	    spin_unlock(&prior->rev, rev);
    }

    return FALSE;
}

status storage_iterate(storage_handle store, record_handle prior,
		       storage_iterate_func iter_fn, void *param)
{
    status st = TRUE;
    if (!iter_fn || (prior && (prior < store->first || prior >= store->limit)))
	return error_invalid_arg("storage_iterate");

    for (prior = (prior ? STORAGE_RECORD(store, prior, 1) : store->first);
	 prior < store->limit; prior = STORAGE_RECORD(store, prior, 1))
	if (FAILED(st = iter_fn(store, prior, param)) || !st)
	    break;

    return st;
}

status storage_sync(storage_handle store)
{
    if (store->is_read_only)
	return error_msg(STORAGE_READ_ONLY,
			 "storage_sync: storage is read-only");

    if (store->seg->seg_size > 0 &&
	msync(store->seg, store->seg->seg_size, MS_SYNC) == -1)
	return error_errno("msync");

    return OK;
}

status storage_reset(storage_handle store)
{
    if (store->is_read_only)
	return error_msg(STORAGE_READ_ONLY,
			 "storage_reset: storage is read-only");

    memset(store->first, 0, (char *)store->limit - (char *)store->first);

    store->seg->q_head = 0;
    if (store->seg->q_mask != (size_t) - 1)
	memset(store->seg->change_q, 0,
	       (store->seg->q_mask + 1) * sizeof(identifier));

    SYNC_SYNCHRONIZE();
    return OK;
}

status storage_delete(const char *mmap_file, boolean force)
{
    if (strncmp(mmap_file, "shm:", 4) == 0) {
	if (shm_unlink(mmap_file + 4) == -1 && (errno != ENOENT || !force))
	    return error_errno("shm_unlink");
    } else {
	if (unlink(mmap_file) == -1 && (errno != ENOENT || !force))
	    return error_errno("unlink");
    }

    return OK;
}

status storage_grow(storage_handle store, storage_handle *pnewstore,
		    const char *new_mmap_file, int open_flags,
		    identifier new_base_id, identifier new_max_id,
		    size_t new_value_size, size_t new_property_size,
		    size_t new_q_capacity)
{
    status st;
    revision rev;
    size_t val_copy_sz, prop_copy_sz;
    record_handle old_r, new_r;
    void *val_copy_buf, *prop_copy_buf;
    struct stat file_stat;

    if (!pnewstore || !new_mmap_file ||
	strcmp(new_mmap_file, store->mmap_file) == 0)
	return error_invalid_arg("storage_grow");

    if (open_flags & ~(O_RDWR | O_CREAT | O_EXCL | O_NOFOLLOW))
	return error_msg(INVALID_OPEN_FLAGS,
			 "storage_grow: invalid open flags: 0%07o",
			 (unsigned)open_flags);

    if (fstat(store->seg_fd, &file_stat) == -1)
	return error_errno("fstat");

    if (FAILED(st = storage_create(pnewstore, new_mmap_file,
				   O_RDWR | open_flags,
				   file_stat.st_mode, FALSE,
				   new_base_id, new_max_id,
				   new_value_size, new_property_size,
				   new_q_capacity,
				   storage_get_description(store))))
	return st;

    val_copy_sz = sizeof(revision) + sizeof(microsec) +
	(store->seg->val_size < (*pnewstore)->seg->val_size
	 ? store->seg->val_size : (*pnewstore)->seg->val_size);

    val_copy_buf = alloca(val_copy_sz);

    if (new_property_size > 0) {
	prop_copy_sz = (store->seg->prop_size < (*pnewstore)->seg->prop_size
			? store->seg->prop_size : (*pnewstore)->seg->prop_size);

	prop_copy_buf = alloca(prop_copy_sz);
    }

    for (old_r = store->first, new_r = (*pnewstore)->first;
	 old_r < store->limit && new_r < (*pnewstore)->limit;
	 old_r = STORAGE_RECORD(store, old_r, 1),
	     new_r = STORAGE_RECORD(*pnewstore, new_r, 1)) {
	do {
	    if (FAILED(st = record_read_lock(old_r, &rev)))
		return st;

	    memcpy(val_copy_buf, old_r, val_copy_sz);

	    if (new_property_size > 0)
		memcpy(prop_copy_buf, (char *)old_r + store->seg->prop_offset,
		       prop_copy_sz);
	} while (rev != record_get_revision(old_r));

	memcpy(new_r, val_copy_buf, val_copy_sz);

	if (new_property_size > 0)
	    memcpy((char *)new_r + (*pnewstore)->seg->prop_offset,
		   prop_copy_buf, prop_copy_sz);
    }

    (*pnewstore)->seg->data_version = store->seg->data_version;
    strcpy((*pnewstore)->seg->description, store->seg->description);

    SYNC_SYNCHRONIZE();

    if (FAILED(st = clock_time(&(*pnewstore)->seg->last_created))) {
	error_save_last();
	storage_destroy(pnewstore);
	error_restore_last();
	return st;
    }

    (*pnewstore)->is_persistent = store->is_persistent;
    return OK;
}

status storage_clear_record(storage_handle store, record_handle rec)
{
    if (store->is_read_only)
	return error_msg(STORAGE_READ_ONLY,
			 "storage_clear_record: storage is read-only");

    if (rec < store->first || rec >= store->limit)
	return error_msg(INVALID_RECORD,
			 "storage_clear_record: invalid record address");

    memset(rec->val, 0, store->seg->val_size);

    if (store->seg->prop_size > 0)
	memset((char *)rec + store->seg->prop_offset, 0,
	       store->seg->prop_size);

    rec->ts = 0;
    spin_unlock(&rec->rev, 0);
    return OK;
}

status storage_copy_record(storage_handle from_store, record_handle from_rec,
			   storage_handle to_store, record_handle to_rec,
			   microsec to_ts, boolean with_prop)
{
    if (to_store->is_read_only)
	return error_msg(STORAGE_READ_ONLY,
			 "storage_copy_record: storage is read-only");

    if (from_rec < from_store->first || from_rec >= from_store->limit ||
	to_rec < to_store->first || to_rec >= to_store->limit)
	return error_msg(INVALID_RECORD,
			 "storage_copy_record: invalid record address");

    if (from_store->seg->val_size != to_store->seg->val_size ||
	(with_prop && from_store->seg->prop_size != to_store->seg->prop_size))
	return error_msg(STORAGE_UNEQUAL,
			 "storage_copy_record: storage is unequal");

    memcpy(to_rec->val, from_rec->val, from_store->seg->val_size);

    if (with_prop && from_store->seg->prop_size > 0)
	memcpy((char *)to_rec + to_store->seg->prop_offset,
	       (char *)from_rec + from_store->seg->prop_offset,
	       from_store->seg->prop_size);

    to_rec->ts = to_ts;
    return OK;
}

void *storage_get_property_ref(storage_handle store, record_handle rec)
{
    return store->seg->prop_offset
	? ((char *)rec + store->seg->prop_offset) : NULL;
}

void *record_get_value_ref(record_handle rec)
{
    return rec->val;
}

microsec record_get_timestamp(record_handle rec)
{
    return rec->ts;
}

void record_set_timestamp(record_handle rec, microsec ts)
{
    rec->ts = ts;
}

revision record_get_revision(record_handle rec)
{
    return rec->rev;
}

void record_set_revision(record_handle rec, revision rev)
{
    spin_unlock(&rec->rev, rev);
}

status record_read_lock(record_handle rec, revision *old_rev)
{
    return spin_read_lock(&rec->rev, old_rev);
}

status record_write_lock(record_handle rec, revision *old_rev)
{
    return spin_write_lock(&rec->rev, old_rev);
}
