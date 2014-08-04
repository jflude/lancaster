#include "circ.h"
#include "error.h"
#include "sync.h"
#include "xalloc.h"

struct circ_t
{
	size_t mask;
	long read_from;
	long write_to;
	void* buf[1];
};

status circ_create(circ_handle* pcirc, size_t capacity)
{
	/* capacity must be a power of 2 */
	if (!pcirc || capacity < 2 || (capacity & (capacity - 1)) != 0) {
		error_invalid_arg("circ_create");
		return FAIL;
	}

	--capacity;

	*pcirc = xmalloc(sizeof(struct circ_t) + capacity * sizeof(void*));
	if (!*pcirc)
		return NO_MEMORY;

	(*pcirc)->mask = capacity;
	(*pcirc)->read_from = (*pcirc)->write_to = 0;
	return OK;
}

void circ_destroy(circ_handle* pcirc)
{
	if (!pcirc || !*pcirc)
		return;

	xfree(*pcirc);
	*pcirc = NULL;
}

size_t circ_get_count(circ_handle circ)
{
	return circ->write_to - circ->read_from;
}

status circ_insert(circ_handle circ, void* val)
{
	long n;
	if ((size_t) (circ->write_to - circ->read_from) == (circ->mask + 1))
		return BLOCKED;

	do {
		n = circ->write_to;
		circ->buf[n & circ->mask] = val;
	} while (!SYNC_BOOL_COMPARE_AND_SWAP(&circ->write_to, n, n + 1));

	return OK;
}

status circ_remove(circ_handle circ, void** pval)
{
	long n;
	if (!pval) {
		error_invalid_arg("circ_remove");
		return FAIL;
	}

	if (circ->write_to == circ->read_from)
		return BLOCKED;

	do {
		n = circ->read_from;
		*pval = circ->buf[n & circ->mask];
	} while (!SYNC_BOOL_COMPARE_AND_SWAP(&circ->read_from, n, n + 1));

	return OK;
}
