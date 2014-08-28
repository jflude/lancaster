#include "accum.h"
#include "error.h"
#include "xalloc.h"
#include <errno.h>

#define NO_TIME (-1)

struct accum
{
	size_t capacity;
	microsec max_age_usec;
	microsec insert_time;
	char* next_free;
	char buf[1];
};

status accum_create(accum_handle* pacc, size_t capacity, microsec max_age_usec)
{
	if (!pacc || capacity == 0 || max_age_usec < 0) {
		error_invalid_arg("accum_create");
		return FAIL;
	}

	*pacc = xmalloc(sizeof(struct accum) + capacity - 1);
	if (!*pacc)
		return NO_MEMORY;

	(*pacc)->capacity = capacity;
	(*pacc)->max_age_usec = max_age_usec;
	(*pacc)->insert_time = NO_TIME;
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
	microsec now;
	status st;

	if (acc->max_age_usec == 0 || acc->insert_time == NO_TIME)
		return FALSE;

	if (FAILED(st = clock_time(&now)))
		return st;

	return (now - acc->insert_time) > acc->max_age_usec;
}

size_t accum_get_available(accum_handle acc)
{
	return acc->capacity - (acc->next_free - acc->buf);
}

status accum_store(accum_handle acc, const void* data, size_t data_sz, void** pstored)
{
	status st;
	if (data_sz == 0 || data_sz > acc->capacity) {
		error_invalid_arg("accum_store");
		return FAIL;
	}

	if (data_sz > (acc->capacity - (acc->next_free - acc->buf)))
		return FALSE;

	if (pstored)
		*pstored = acc->next_free;

	if (data)
		memcpy(acc->next_free, data, data_sz);

	acc->next_free += data_sz;

	if (acc->insert_time == NO_TIME && FAILED(st = clock_time(&acc->insert_time)))
		return st;

	return TRUE;
}

status accum_get_batched(accum_handle acc, const void** pdata, size_t* psz)
{
	size_t used;
	if (!pdata || !psz) {
		error_invalid_arg("accum_get_batched");
		return FAIL;
	}

	used = acc->next_free - acc->buf;
	if (used == 0)
		return FALSE;

	*pdata = acc->buf;
	*psz = used;
	return TRUE;
}

void accum_clear(accum_handle acc)
{
	acc->next_free = acc->buf;
	acc->insert_time = NO_TIME;
}
