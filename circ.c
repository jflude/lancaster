#include "circ.h"
#include "barrier.h"
#include "error.h"
#include "xalloc.h"
#include "yield.h"

struct circ_t
{
	unsigned mask;
	unsigned read_from;
	unsigned write_to;
	void* buf[1];
};

status circ_create(circ_handle* pcirc, unsigned capacity)
{
	if (!pcirc || capacity == 0 || (capacity & (capacity - 1)) != 0) {
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

unsigned circ_get_count(circ_handle circ)
{
	return circ->write_to - circ->read_from;
}

status circ_insert(circ_handle circ, void* val)
{
	if ((circ->write_to - circ->read_from) == (circ->mask + 1))
		return BLOCKED;

	circ->buf[circ->write_to & circ->mask] = val;
	++circ->write_to;
	COMPILER_BARRIER();
	return OK;
}

status circ_remove(circ_handle circ, void** pval)
{
	if (!pval) {
		error_invalid_arg("circ_remove");
		return FAIL;
	}

	if (circ->write_to == circ->read_from)
		return BLOCKED;

	*pval = circ->buf[circ->read_from & circ->mask];
	++circ->read_from;
	return OK;
}
