#include "storage.h"
#include "clock.h"
#include "error.h"
#include "spin.h"
#include "xalloc.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>

struct record_t
{
	volatile sequence seq;
	void* confl;
	char val[1];
};

struct segment_t
{
	unsigned magic;
	pid_t owner_pid;
	size_t mmap_size;
	size_t hdr_size;
	size_t rec_size;
	size_t val_size;
	identifier base_id;
	identifier max_id;
	microsec_t last_created;
	microsec_t last_send_recv;
	volatile int send_recv_ver;
	size_t q_mask;
	long q_head;
	identifier change_q[1];
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
#define MAGIC_NUMBER 0x0C0FFEE0

static void clear_change_q(storage_handle store)
{
	SPIN_CREATE(&store->seg->send_recv_ver);
	store->seg->last_send_recv = 0;

	store->seg->q_head = 0;
	if (store->seg->q_mask != -1u)
		memset(store->seg->change_q, -1, sizeof(store->seg->change_q[0]) * (store->seg->q_mask + 1));
}

status storage_create(storage_handle* pstore, const char* mmap_file, int open_flags, size_t q_capacity,
					  identifier base_id, identifier max_id, size_t val_size)
{
	/* q_capacity must be a power of 2 */
	size_t rec_sz, hdr_sz, seg_sz;
	if (!pstore || max_id <= base_id || val_size == 0 || open_flags & ~(O_CREAT | O_EXCL | O_TRUNC) ||
		q_capacity == 1 || (q_capacity & (q_capacity - 1)) != 0) {
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
	(*pstore)->seg->owner_pid = getpid();
	(*pstore)->seg->mmap_size = seg_sz;
	(*pstore)->seg->hdr_size = hdr_sz;
	(*pstore)->seg->rec_size = rec_sz;
	(*pstore)->seg->val_size = val_size;
	(*pstore)->seg->base_id = base_id;
	(*pstore)->seg->max_id = max_id;

	(*pstore)->array = (void*) (((char*) (*pstore)->seg) + hdr_sz);
	(*pstore)->limit = RECORD_ADDR(*pstore, (*pstore)->array, max_id - base_id);

	(*pstore)->seg->q_mask = q_capacity - 1;
	clear_change_q(*pstore);

	return clock_time(&(*pstore)->seg->last_created);
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
		fd = shm_open(mmap_file + 4, O_RDONLY, 0);
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
		fd = open(mmap_file, O_RDONLY);
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

	(*pstore)->seg = mmap(NULL, seg_sz, PROT_READ, MAP_SHARED, fd, 0);
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

boolean storage_is_owner(storage_handle store)
{
	return store->is_seg_owner;
}

pid_t storage_get_owner_pid(storage_handle store)
{
	return store->seg->owner_pid;
}

record_handle storage_get_array(storage_handle store)
{
	return store->array;
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
	return offsetof(struct record_t, val);
}

microsec_t storage_get_creation_time(storage_handle store)
{
	return store->seg->last_created;
}

microsec_t storage_get_send_recv_time(storage_handle store)
{
	microsec_t t;
	int ver;
	do {
		SPIN_READ_LOCK(&store->seg->send_recv_ver, ver);
		t = store->seg->last_send_recv;
	} while (ver != store->seg->send_recv_ver);

	return t;
}

status storage_set_send_recv_time(storage_handle store, microsec_t when)
{
	int ver;
	if (!store->is_seg_owner) {
		errno = EPERM;
		error_errno("storage_set_send_recv_time");
		return FAIL;
	}

	SPIN_WRITE_LOCK(&store->seg->send_recv_ver, ver);
	store->seg->last_send_recv = when;
	SPIN_UNLOCK(&store->seg->send_recv_ver, (ver + 1) & INT_MAX);
	return OK;
}

const identifier* storage_get_queue_base_ref(storage_handle store)
{
	return store->seg->change_q;
}

const long* storage_get_queue_head_ref(storage_handle store)
{
	return &store->seg->q_head;
}

size_t storage_get_queue_capacity(storage_handle store)
{
	return store->seg->q_mask + 1;
}

long storage_get_queue_head(storage_handle store)
{
	return store->seg->q_head;
}

identifier storage_read_queue(storage_handle store, long index)
{
	return store->seg->change_q[index & store->seg->q_mask];
}

status storage_write_queue(storage_handle store, identifier id)
{
	long n;
	if (!store->is_seg_owner) {
		errno = EPERM;
		error_errno("storage_write_queue");
		return FAIL;
	}

	if (store->seg->q_mask == -1u) {
		errno = ENOBUFS;
		error_errno("storage_write_queue");
		return FAIL;
	}

	do {
		n = store->seg->q_head;
		store->seg->change_q[n & store->seg->q_mask] = id;
	} while (!SYNC_BOOL_COMPARE_AND_SWAP(&store->seg->q_head, n, n + 1));

	if (store->seg->q_head < 0) {
		errno = EOVERFLOW;
		error_errno("storage_write_queue");
		return FAIL;
	}

	return OK;
}

status storage_get_id(storage_handle store, record_handle rec, identifier* pident)
{
	if (!pident) {
		error_invalid_arg("storage_get_id");
		return FAIL;
	}

	if (rec < store->array || rec >= store->limit) {
		errno = EBADSLT;
		error_errno("storage_get_id");
		return FAIL;
	}

	*pident = ((char*) rec - (char*) store->array) / store->seg->rec_size;
	return OK;
}

status storage_get_record(storage_handle store, identifier id, record_handle* prec)
{
	if (!prec) {
		error_invalid_arg("storage_get_record");
		return FAIL;
	}

	if (id < store->seg->base_id || id >= store->seg->max_id) {
		errno = EBADSLT;
		error_errno("storage_get_record");
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

	for (prev = (prev ? RECORD_ADDR(store, prev, 1) : store->array); prev < store->limit; prev = RECORD_ADDR(store, prev, 1)) {
		st = iter_fn(prev, param);
		if (FAILED(st) || !st)
			break;
	}

	return st;
}

status storage_sync(storage_handle store)
{
	if (store->seg->mmap_size > 0 && msync(store->seg, store->seg->mmap_size, MS_SYNC) == -1) {
		error_errno("msync");
		return FAIL;
	}

	return OK;
}

status storage_reset(storage_handle store)
{
	if (!store->is_seg_owner) {
		errno = EPERM;
		error_errno("storage_reset");
		return FAIL;
	}

	clear_change_q(store);
	memset(store->array, 0, (char*) store->limit - (char*) store->array);
	return OK;
}

void* record_get_value_ref(record_handle rec)
{
	return rec->val;
}

sequence record_get_sequence(record_handle rec)
{
	return rec->seq;
}

void record_set_sequence(record_handle rec, sequence seq)
{
	SPIN_UNLOCK(&rec->seq, seq);
}

void* record_get_conflated_ref(record_handle rec)
{
	return rec->confl;
}

void record_set_conflated_ref(record_handle rec, void* confl)
{
	rec->confl = confl;
}

sequence record_read_lock(record_handle rec)
{
	sequence seq;
	SPIN_READ_LOCK(&rec->seq, seq);
	return seq;
}

sequence record_write_lock(record_handle rec)
{
	sequence seq;
	SPIN_WRITE_LOCK(&rec->seq, seq);
	return seq;
}
