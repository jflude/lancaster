#include "storage.h"
#include "error.h"
#include "xalloc.h"
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>

struct record_t
{
	spin_lock_t lock;
	int id;
	long seq;
	void* confl;
	char val[1];
};

struct segment_t
{
	unsigned magic;
	size_t mmap_size;
	size_t hdr_size;
	size_t rec_size;
	size_t val_size;
	int base_id;
	int max_id;
	unsigned q_mask;
	unsigned q_head;
	int change_q[1];
};

struct storage_t
{
	record_handle array;
	record_handle limit;
	struct segment_t* seg;
	char* mmap_file;
	boolean is_seg_owner;
};

#define RECORD_ADDR(stg, base, idx) ((record_handle) ((char*) base + (idx) * (stg)->seg->rec_size))
#define MAGIC_NUMBER 0xC0FFEE

status storage_create(storage_handle* pstore, const char* mmap_file, unsigned q_capacity, int base_id, int max_id, size_t val_size)
{
	size_t rec_sz, hdr_sz, seg_sz;
	if (!pstore || max_id <= base_id || val_size == 0 || q_capacity == 1 || (q_capacity & (q_capacity - 1)) != 0) {
		error_invalid_arg("storage_create");
		return FAIL;
	}

	*pstore = XMALLOC(struct storage_t);
	if (!*pstore)
		return NO_MEMORY;

	BZERO(*pstore);
	(*pstore)->is_seg_owner = TRUE;

	rec_sz = ALIGNED_SIZE(struct record_t, DEFAULT_ALIGNMENT, val, val_size);
	hdr_sz = ALIGNED_SIZE(struct segment_t, DEFAULT_ALIGNMENT, change_q, q_capacity > 0 ? q_capacity : 1);
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
		shm_loop:
			fd = shm_open(mmap_file + 4, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
			if (fd == -1) {
				if (errno == EINTR)
					goto shm_loop;

				error_errno("shm_open");
				storage_destroy(pstore);
				return FAIL;
			}
		} else {
		open_loop:
			fd = open(mmap_file, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
			if (fd == -1) {
				if (errno == EINTR)
					goto open_loop;

				error_errno("open");
				storage_destroy(pstore);
				return FAIL;
			}
		}

	trunc_loop:
		if (ftruncate(fd, seg_sz) == -1) {
			if (errno == EINTR)
				goto trunc_loop;

			error_errno("ftruncate");
			close(fd);
			storage_destroy(pstore);
			return FAIL;
		}

		(*pstore)->seg = mmap(NULL, seg_sz, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
		if ((*pstore)->seg == MAP_FAILED) {
			error_errno("mmap");
			(*pstore)->seg = NULL;
			close(fd);
			storage_destroy(pstore);
			return FAIL;
		}

		(*pstore)->mmap_file = xstrdup(mmap_file);

		if (close(fd) == -1) {
			error_errno("close");
			storage_destroy(pstore);
			return FAIL;
		}
	}

	(*pstore)->seg->magic = MAGIC_NUMBER;
	(*pstore)->seg->mmap_size = seg_sz;
	(*pstore)->seg->hdr_size = hdr_sz;
	(*pstore)->seg->rec_size = rec_sz;
	(*pstore)->seg->val_size = val_size;
	(*pstore)->seg->base_id = base_id;
	(*pstore)->seg->max_id = max_id;
	(*pstore)->seg->q_mask = q_capacity - 1;

	(*pstore)->array = (void*) (((char*) (*pstore)->seg) + hdr_sz);
	(*pstore)->limit = RECORD_ADDR(*pstore, (*pstore)->array, max_id - base_id);

	if ((*pstore)->seg->mmap_size > 0 && msync((*pstore)->seg, (*pstore)->seg->mmap_size, MS_SYNC) == -1) {
		error_errno("msync");
		storage_destroy(pstore);
		return FAIL;
	}

	return OK;
}

status storage_open(storage_handle* pstore, const char* mmap_file)
{
	int fd;
	size_t seg_sz;
	if (!pstore || !mmap_file) {
		error_invalid_arg("storage_open");
		return FAIL;
	}

	*pstore = XMALLOC(struct storage_t);
	if (!*pstore)
		return NO_MEMORY;

	BZERO(*pstore);
	(*pstore)->is_seg_owner = FALSE;

	if (strncmp(mmap_file, "shm:", 4) == 0) {
	shm_loop:
		fd = shm_open(mmap_file + 4, O_RDWR, 0);
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
		fd = open(mmap_file, O_RDWR);
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

		if ((unsigned) file_stat.st_size < sizeof(struct segment_t)) {
			errno = EILSEQ;
			error_errno("storage_open");
			close(fd);
			storage_destroy(pstore);
			return FAIL;
		}
	}

	(*pstore)->seg = mmap(NULL, sizeof(struct segment_t), PROT_READ, MAP_SHARED, fd, 0);
	if ((*pstore)->seg == MAP_FAILED) {
		error_errno("mmap");
		(*pstore)->seg = NULL;
		close(fd);
		storage_destroy(pstore);
		return FAIL;
	}

	if ((*pstore)->seg->magic != MAGIC_NUMBER) {
		errno = EILSEQ;
		error_errno("storage_open");
		close(fd);
		storage_destroy(pstore);
		return FAIL;
	}

	seg_sz = (*pstore)->seg->mmap_size;
	if (munmap((*pstore)->seg, sizeof(struct segment_t)) == -1) {
		error_errno("munmap");
		close(fd);
		storage_destroy(pstore);
		return FAIL;
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

	(*pstore)->array = (void*) (((char*) (*pstore)->seg) + (*pstore)->seg->hdr_size);
	(*pstore)->limit = RECORD_ADDR(*pstore, (*pstore)->array, (*pstore)->seg->max_id - (*pstore)->seg->base_id);
	return OK;
}

void storage_destroy(storage_handle* pstore)
{
	if (!pstore || !*pstore)
		return;

	if ((*pstore)->is_seg_owner && (*pstore)->seg) {
		if ((*pstore)->seg->mmap_size > 0) {
			if (munmap((*pstore)->seg, (*pstore)->seg->mmap_size) == -1)
				error_errno("munmap");
			else if (strncmp((*pstore)->mmap_file, "shm:", 4) == 0 && shm_unlink((*pstore)->mmap_file + 4) == -1)
				error_errno("shm_unlink");

			xfree((*pstore)->mmap_file);
		} else
			xfree((*pstore)->seg);
	}

	xfree(*pstore);
	*pstore = NULL;
}

boolean storage_is_segment_owner(storage_handle store)
{
	return store->is_seg_owner;
}

record_handle storage_get_array(storage_handle store)
{
	return store->array;
}

int storage_get_base_id(storage_handle store)
{
	return store->seg->base_id;
}

int storage_get_max_id(storage_handle store)
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
	return offsetof(struct record_t, val);
}

const int* storage_get_queue_base_address(storage_handle store)
{
	return store->seg->change_q;
}

const unsigned* storage_get_queue_head_address(storage_handle store)
{
	return &store->seg->q_head;
}

unsigned storage_get_queue_capacity(storage_handle store)
{
	return store->seg->q_mask + 1;
}

unsigned storage_get_queue_head(storage_handle store)
{
	return store->seg->q_head;
}

int storage_read_queue(storage_handle store, unsigned index)
{
	return store->seg->change_q[index & store->seg->q_mask];
}

status storage_write_queue(storage_handle store, int id)
{
	if (!store->is_seg_owner) {
		errno = EPERM;
		error_errno("storage_write_queue");
		return FAIL;
	}

	if (store->seg->q_mask == (unsigned) -1) {
		errno = ENOBUFS;
		error_errno("storage_write_queue");
		return FAIL;
	}

	store->seg->change_q[store->seg->q_head++ & store->seg->q_mask] = id;
	return OK;
}

status storage_lookup(storage_handle store, int id, record_handle* prec)
{
	if (!prec || id < store->seg->base_id || id >= store->seg->max_id) {
		error_invalid_arg("storage_lookup");
		return FAIL;
	}

	*prec = RECORD_ADDR(store, store->array, id - store->seg->base_id);
	return OK;
}

status storage_iterate(storage_handle store, storage_iterate_func iter_fn, record_handle prev, void* param)
{
	status st = TRUE;
	if (!iter_fn) {
		error_invalid_arg("storage_iterate");
		return FAIL;
	}

	if (prev)
		prev = RECORD_ADDR(store, prev, 1);
	else
		prev = store->array;

	for (; prev < store->limit; prev = RECORD_ADDR(store, prev, 1)) {
		st = iter_fn(prev, param);
		if (FAILED(st) || !st)
			break;
	}

	return st;
}

status storage_reset(storage_handle store)
{
	int i;
	record_handle rec;

	if (!store->is_seg_owner) {
		errno = EPERM;
		error_errno("storage_reset");
		return FAIL;
	}

	rec = store->array;
	for (i = store->seg->base_id; i < store->seg->max_id; ++i) {
		SPIN_CREATE(&rec->lock);
		rec->id = i;
		rec->seq = 0;
		rec->confl = NULL;
		memset(rec->val, 0, store->seg->val_size);

		rec = RECORD_ADDR(store, rec, 1);
	}

	for (i = store->seg->q_mask; i >= 0; --i)
		store->seg->change_q[i] = -1;

	store->seg->q_head = 0;
	return OK;
}

int record_get_id(record_handle rec)
{
	return rec->id;
}

void* record_get_value(record_handle rec)
{
	return rec->val;
}

long record_get_sequence(record_handle rec)
{
	return rec->seq;
}

void record_set_sequence(record_handle rec, long seq)
{
	rec->seq = seq;
}

void* record_get_conflated(record_handle rec)
{
	return rec->confl;
}

void record_set_conflated(record_handle rec, void* confl)
{
	rec->confl = confl;
}
