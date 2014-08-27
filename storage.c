#include "storage.h"
#include "clock.h"
#include "error.h"
#include "spin.h"
#include "xalloc.h"
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>

struct record
{
	volatile version ver;
	char val[1];
};

struct segment
{
	unsigned magic;
	char description[256];
	size_t mmap_size;
	size_t hdr_size;
	size_t rec_size;
	size_t val_size;
	identifier base_id;
	identifier max_id;
	microsec last_created;
	microsec last_touched;
	volatile version last_touched_ver;
	size_t q_mask;
	queue_index q_head;
	identifier change_q[1];
};

struct storage
{
	struct segment* seg;
	record_handle first;
	record_handle limit;
	char* mmap_file;
	boolean is_read_only;
};

#define RECORD_ADDR(stg, base, idx) ((record_handle) ((char*) base + (idx) * (stg)->seg->rec_size))
#define MAGIC_NUMBER 0x0C0FFEE0

status storage_create(storage_handle* pstore, const char* mmap_file, int open_flags,
					  identifier base_id, identifier max_id, size_t value_size, size_t q_capacity)
{
	/* q_capacity must be a power of 2 */
	size_t rec_sz, hdr_sz, seg_sz;
	if (!pstore || open_flags & ~(O_CREAT | O_EXCL | O_TRUNC) || max_id <= base_id || value_size == 0 ||
		q_capacity == 1 || (q_capacity & (q_capacity - 1)) != 0) {
		error_invalid_arg("storage_create");
		return FAIL;
	}

	*pstore = XMALLOC(struct storage);
	if (!*pstore)
		return NO_MEMORY;

	BZERO(*pstore);

	rec_sz = ALIGNED_SIZE(struct record, DEFAULT_ALIGNMENT, val, value_size);
	hdr_sz = ALIGNED_SIZE(struct segment, DEFAULT_ALIGNMENT, change_q, q_capacity > 0 ? q_capacity : 1);
	seg_sz = hdr_sz + rec_sz * (max_id - base_id);

	if (!mmap_file) {
		(*pstore)->seg = xmalloc(seg_sz);
		if (!(*pstore)->seg) {
			storage_destroy(pstore);
			return NO_MEMORY;
		}

		seg_sz = 0;
	} else {
		int fd;
		size_t page_sz = sysconf(_SC_PAGESIZE);
		seg_sz = (seg_sz + page_sz - 1) & ~(page_sz - 1);

		if (strncmp(mmap_file, "shm:", 4) == 0) {
			open_flags &= ~O_TRUNC;
		shm_loop:
			fd = shm_open(mmap_file + 4, open_flags | O_RDWR, S_IRUSR | S_IWUSR);
			if (fd == -1) {
				if (errno == EINTR)
					goto shm_loop;

				error_errno("shm_open");
				storage_destroy(pstore);
				return FAIL;
			}
		} else {
		open_loop:
			fd = open(mmap_file, open_flags | O_RDWR, S_IRUSR | S_IWUSR);
			if (fd == -1) {
				if (errno == EINTR)
					goto open_loop;

				error_errno("open");
				storage_destroy(pstore);
				return FAIL;
			}
		}

		if (open_flags & (O_CREAT | O_TRUNC)) {
		trunc_loop:
			if (ftruncate(fd, seg_sz) == -1) {
				if (errno == EINTR)
					goto trunc_loop;

				error_errno("ftruncate");
				close(fd);
				storage_destroy(pstore);
				return FAIL;
			}
		} else {
			struct stat file_stat;
			if (fstat(fd, &file_stat) == -1) {
				error_errno("fstat");
				close(fd);
				storage_destroy(pstore);
				return FAIL;
			}

			if ((size_t) file_stat.st_size != seg_sz) {
				error_msg("storage_create: storage is unequal size", STORAGE_CORRUPTED);
				close(fd);
				storage_destroy(pstore);
				return STORAGE_CORRUPTED;
			}
		}

		(*pstore)->seg = mmap(NULL, seg_sz, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
		if ((*pstore)->seg == MAP_FAILED) {
			error_errno("mmap");
			(*pstore)->seg = NULL;
			close(fd);
			storage_destroy(pstore);
			return FAIL;
		}

		if (close(fd) == -1) {
			error_errno("close");
			storage_destroy(pstore);
			return FAIL;
		}

		(*pstore)->mmap_file = xstrdup(mmap_file);
		if (!(*pstore)->mmap_file) {
			storage_destroy(pstore);
			return NO_MEMORY;
		}
	}

	if (!mmap_file || (open_flags & (O_CREAT | O_TRUNC))) {
		(*pstore)->seg->magic = MAGIC_NUMBER;
		(*pstore)->seg->mmap_size = seg_sz;
		(*pstore)->seg->hdr_size = hdr_sz;
		(*pstore)->seg->rec_size = rec_sz;
		(*pstore)->seg->val_size = value_size;
		(*pstore)->seg->base_id = base_id;
		(*pstore)->seg->max_id = max_id;
		(*pstore)->seg->q_mask = q_capacity - 1;

		BZERO((*pstore)->seg->description);
	}

	(*pstore)->seg->q_head = 0;
	if ((*pstore)->seg->q_mask != -1u)
		memset((*pstore)->seg->change_q, -1, ((*pstore)->seg->q_mask + 1) * sizeof(identifier));

	(*pstore)->first = (void*) (((char*) (*pstore)->seg) + hdr_sz);
	(*pstore)->limit = RECORD_ADDR(*pstore, (*pstore)->first, max_id - base_id);

	if (mmap_file && (open_flags & (O_CREAT | O_EXCL)) != (O_CREAT | O_EXCL)) {
		record_handle r;
		for (r = (*pstore)->first; r < (*pstore)->limit; r = RECORD_ADDR(*pstore, r, 1))
			if (r->ver < 0)
				r->ver &= ~SPIN_MASK(r->ver);

		SYNC_SYNCHRONIZE();
	}

	return clock_time(&(*pstore)->seg->last_created);
}

status storage_open(storage_handle* pstore, const char* mmap_file, int open_flags)
{
	int fd, mmap_flags = PROT_READ;
	size_t seg_sz;
	if (!pstore || (open_flags != O_RDONLY && open_flags != O_RDWR) || !mmap_file) {
		error_invalid_arg("storage_open");
		return FAIL;
	}

	*pstore = XMALLOC(struct storage);
	if (!*pstore)
		return NO_MEMORY;

	BZERO(*pstore);

	if (open_flags == O_RDONLY)
		(*pstore)->is_read_only = TRUE;
	else
		mmap_flags |= PROT_WRITE;

	if (strncmp(mmap_file, "shm:", 4) == 0) {
	shm_loop:
		fd = shm_open(mmap_file + 4, open_flags, 0);
		if (fd == -1) {
			if (errno == EINTR)
				goto shm_loop;

			error_errno("shm_open");
			storage_destroy(pstore);
			return FAIL;
		}
	} else {
		struct stat file_stat;
	open_loop:
		fd = open(mmap_file, open_flags);
		if (fd == -1) {
			if (errno == EINTR)
				goto open_loop;

			error_errno("open");
			storage_destroy(pstore);
			return FAIL;
		}

		if (fstat(fd, &file_stat) == -1) {
			error_errno("fstat");
			close(fd);
			storage_destroy(pstore);
			return FAIL;
		}

		if ((size_t) file_stat.st_size < sizeof(struct segment)) {
			error_msg("storage_open: storage is truncated", STORAGE_CORRUPTED);
			close(fd);
			storage_destroy(pstore);
			return STORAGE_CORRUPTED;
		}
	}

	(*pstore)->seg = mmap(NULL, sizeof(struct segment), PROT_READ, MAP_SHARED, fd, 0);
	if ((*pstore)->seg == MAP_FAILED) {
		error_errno("mmap");
		(*pstore)->seg = NULL;
		close(fd);
		storage_destroy(pstore);
		return FAIL;
	}

	if ((*pstore)->seg->magic != MAGIC_NUMBER) {
		error_msg("storage_open: storage is corrupt", STORAGE_CORRUPTED);
		close(fd);
		storage_destroy(pstore);
		return STORAGE_CORRUPTED;
	}

	seg_sz = (*pstore)->seg->mmap_size;
	if (munmap((*pstore)->seg, sizeof(struct segment)) == -1) {
		error_errno("munmap");
		close(fd);
		storage_destroy(pstore);
		return FAIL;
	}

	(*pstore)->seg = mmap(NULL, seg_sz, mmap_flags, MAP_SHARED, fd, 0);
	if ((*pstore)->seg == MAP_FAILED) {
		error_errno("mmap");
		(*pstore)->seg = NULL;
		close(fd);
		storage_destroy(pstore);
		return FAIL;
	}

	if (close(fd) == -1) {
		error_errno("close");
		storage_destroy(pstore);
		return FAIL;
	}

	(*pstore)->mmap_file = xstrdup(mmap_file);
	if (!(*pstore)->mmap_file) {
		storage_destroy(pstore);
		return NO_MEMORY;
	}

	(*pstore)->first = (void*) (((char*) (*pstore)->seg) + (*pstore)->seg->hdr_size);
	(*pstore)->limit = RECORD_ADDR(*pstore, (*pstore)->first, (*pstore)->seg->max_id - (*pstore)->seg->base_id);
	return OK;
}

void storage_destroy(storage_handle* pstore)
{
	if (!pstore || !*pstore)
		return;

	if ((*pstore)->seg) {
		if ((*pstore)->seg->mmap_size > 0) {
			if (munmap((*pstore)->seg, (*pstore)->seg->mmap_size) == -1)
				error_errno("munmap");
			else if (!(*pstore)->is_read_only &&
					 strncmp((*pstore)->mmap_file, "shm:", 4) == 0 &&
					 shm_unlink((*pstore)->mmap_file + 4) == -1)
				error_errno("shm_unlink");
		} else
			xfree((*pstore)->seg);
	}

	xfree((*pstore)->mmap_file);
	xfree(*pstore);
	*pstore = NULL;
}

boolean storage_is_read_only(storage_handle store)
{
	return store->is_read_only;
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

size_t storage_get_value_size(storage_handle store)
{
	return store->seg->val_size;
}

size_t storage_get_value_offset(storage_handle store)
{
	(void) store;
	return offsetof(struct record, val);
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
	if (!desc) {
		error_invalid_arg("storage_set_description");
		return FAIL;
	}

	if (store->is_read_only) {
		error_msg("storage_set_description: storage is read-only", STORAGE_READ_ONLY);
		return STORAGE_READ_ONLY;
	}

	if (strlen(desc) >= sizeof(store->seg->description)) {
		error_msg("storage_set_description: description too long", BUFFER_TOO_SMALL);
		return BUFFER_TOO_SMALL;
	}

	strcpy(store->seg->description, desc);
	return OK;
}

microsec storage_get_created_time(storage_handle store)
{
	return store->seg->last_created;
}

microsec storage_get_touched_time(storage_handle store)
{
	microsec t;
	version ver;
	do {
		SPIN_READ_LOCK(&store->seg->last_touched_ver, ver);
		t = store->seg->last_touched;
	} while (ver != store->seg->last_touched_ver);

	return t;
}

status storage_touch(storage_handle store)
{
	status st;
	version ver;
	microsec now;

	if (store->is_read_only) {
		error_msg("storage_touch: storage is read-only", STORAGE_READ_ONLY);
		return STORAGE_READ_ONLY;
	}

	if (FAILED(st = clock_time(&now)))
		return st;

	SPIN_WRITE_LOCK(&store->seg->last_touched_ver, ver);
	store->seg->last_touched = now;
	SPIN_UNLOCK(&store->seg->last_touched_ver, NEXT_VER(ver));
	return st;
}

const identifier* storage_get_queue_base_ref(storage_handle store)
{
	return store->seg->change_q;
}

const queue_index* storage_get_queue_head_ref(storage_handle store)
{
	return &store->seg->q_head;
}

size_t storage_get_queue_capacity(storage_handle store)
{
	return store->seg->q_mask + 1;
}

queue_index storage_get_queue_head(storage_handle store)
{
	return store->seg->q_head;
}

identifier storage_read_queue(storage_handle store, queue_index index)
{
	return store->seg->change_q[index & store->seg->q_mask];
}

status storage_write_queue(storage_handle store, identifier id)
{
	queue_index n;
	if (store->is_read_only) {
		error_msg("storage_write_queue: storage is read-only", STORAGE_READ_ONLY);
		return STORAGE_READ_ONLY;
	}

	if (store->seg->q_mask == -1u) {
		error_msg("storage_write_queue: no change queue", STORAGE_NO_CHANGE_QUEUE);
		return STORAGE_NO_CHANGE_QUEUE;
	}

	do {
		n = store->seg->q_head;
		store->seg->change_q[n & store->seg->q_mask] = id;
	} while (!SYNC_BOOL_COMPARE_AND_SWAP(&store->seg->q_head, n, n + 1));

	if ((n + 1) < 0) {
		error_msg("storage_write_queue: index sign overflow", SIGN_OVERFLOW);
		return SIGN_OVERFLOW;
	}

	return OK;
}

status storage_get_id(storage_handle store, record_handle rec, identifier* pident)
{
	if (!pident) {
		error_invalid_arg("storage_get_id");
		return FAIL;
	}

	if (rec < store->first || rec >= store->limit) {
		error_msg("storage_get_id: invalid record address", STORAGE_INVALID_SLOT);
		return STORAGE_INVALID_SLOT;
	}

	*pident = ((char*) rec - (char*) store->first) / store->seg->rec_size;
	return OK;
}

status storage_get_record(storage_handle store, identifier id, record_handle* prec)
{
	if (!prec) {
		error_invalid_arg("storage_get_record");
		return FAIL;
	}

	if (id < store->seg->base_id || id >= store->seg->max_id) {
		error_msg("storage_get_record: invalid identifier", STORAGE_INVALID_SLOT);
		return STORAGE_INVALID_SLOT;
	}

	*prec = RECORD_ADDR(store, store->first, id - store->seg->base_id);
	return OK;
}

status storage_iterate(storage_handle store, storage_iterate_func iter_fn, record_handle prev, void* param)
{
	status st = TRUE;
	if (!iter_fn) {
		error_invalid_arg("storage_iterate");
		return FAIL;
	}

	for (prev = (prev ? RECORD_ADDR(store, prev, 1) : store->first); prev < store->limit; prev = RECORD_ADDR(store, prev, 1)) {
		st = iter_fn(prev, param);
		if (FAILED(st) || !st)
			break;
	}

	return st;
}

status storage_sync(storage_handle store)
{
	if (store->is_read_only) {
		error_msg("storage_sync: storage is read-only", STORAGE_READ_ONLY);
		return STORAGE_READ_ONLY;
	}

	if (store->seg->mmap_size > 0 && msync(store->seg, store->seg->mmap_size, MS_SYNC) == -1) {
		error_errno("msync");
		return FAIL;
	}

	return OK;
}

status storage_reset(storage_handle store)
{
	if (store->is_read_only) {
		error_msg("storage_reset: storage is read-only", STORAGE_READ_ONLY);
		return STORAGE_READ_ONLY;
	}

	memset(store->first, 0, (char*) store->limit - (char*) store->first);

	store->seg->q_head = 0;
	if (store->seg->q_mask != -1u)
		memset(store->seg->change_q, -1, (store->seg->q_mask + 1) * sizeof(identifier));

	SYNC_SYNCHRONIZE();
	return OK;
}

void* record_get_value_ref(record_handle rec)
{
	return rec->val;
}

version record_get_version(record_handle rec)
{
	return rec->ver;
}

void record_set_version(record_handle rec, version ver)
{
	SPIN_UNLOCK(&rec->ver, ver);
}

version record_read_lock(record_handle rec)
{
	version old_ver;
	SPIN_READ_LOCK(&rec->ver, old_ver);
	return old_ver;
}

version record_write_lock(record_handle rec)
{
	version old_ver;
	SPIN_WRITE_LOCK(&rec->ver, old_ver);
	return old_ver;
}
