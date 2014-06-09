#include "accum.h"
#include "error.h"
#include "xalloc.h"
#include <errno.h>
#include <sys/time.h>

#define NO_TIME ((time_t) -1)

struct accum_t
{
	size_t capacity;
	long max_age;
	struct timeval insert_time;
	char* next_free;
	char buf[1];
};

status accum_create(accum_handle* pacc, size_t capacity, long max_age_usec)
{
	if (!pacc || capacity == 0 || max_age_usec < 0) {
		error_invalid_arg("accum_create");
		return FAIL;
	}

	*pacc = xmalloc(sizeof(struct accum_t) + capacity - 1);
	if (!*pacc)
		return NO_MEMORY;

	(*pacc)->capacity = capacity;
	(*pacc)->max_age = max_age_usec;
	(*pacc)->insert_time.tv_sec = NO_TIME;
	(*pacc)->next_free = (*pacc)->buf;
	return OK;
}

void accum_destroy(accum_handle* pacc)
{
	if (!pacc || !*pacc)
		return;

	xfree(*pacc);
	*pacc = NULL;
}

boolean accum_is_empty(accum_handle acc)
{
	return acc->next_free == acc->buf;
}

status accum_is_stale(accum_handle acc)
{
	struct timeval tv;
	if (acc->max_age <= 0 || acc->insert_time.tv_sec == NO_TIME)
		return FALSE;

	if (gettimeofday(&tv, NULL) == -1) {
		error_errno("gettimeofday");
		return FAIL;
	}

	return acc->max_age < (1000000 * (tv.tv_sec - acc->insert_time.tv_sec) + tv.tv_usec - acc->insert_time.tv_usec);
}

size_t accum_get_avail(accum_handle acc)
{
	return acc->capacity - (acc->next_free - acc->buf);
}

status accum_store(accum_handle acc, const void* data, size_t size, void** pstored)
{
	if (!data || size == 0 || size > acc->capacity) {
		error_invalid_arg("accum_store");
		return FAIL;
	}

	if (size > (acc->capacity - (acc->next_free - acc->buf)))
		return FALSE;

	if (pstored)
		*pstored = acc->next_free;

	memcpy(acc->next_free, data, size);
	acc->next_free += size;

	if (acc->insert_time.tv_sec == NO_TIME && gettimeofday(&acc->insert_time, NULL) == -1) {
		error_errno("gettimeofday");
		return FAIL;
	}

	return TRUE;
}

status accum_get_batched(accum_handle acc, const void** pdata, size_t* psize)
{
	size_t used;
	if (!pdata || !psize) {
		error_invalid_arg("accum_get_batched");
		return FAIL;
	}

	used = acc->next_free - acc->buf;
	if (used == 0)
		return FALSE;

	*pdata = acc->buf;
	*psize = used;
	return TRUE;
}

void accum_clear(accum_handle acc)
{
	acc->next_free = acc->buf;
	acc->insert_time.tv_sec = NO_TIME;
}
