#include "circ.h"
#include "error.h"
#include "xalloc.h"
#include "yield.h"

struct circ_t
{
	unsigned capacity;
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

	*pcirc = xmalloc(sizeof(struct circ_t) + (capacity - 1) * sizeof(void*));
	if (!*pcirc)
		return NO_MEMORY;

	(*pcirc)->capacity = capacity;
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

unsigned circ_size(circ_handle circ)
{
	return circ->write_to - circ->read_from;
}

status circ_insert(circ_handle circ, void* val)
{
	if ((circ->write_to - circ->read_from) == circ->capacity)
		return BLOCKED;

	circ->buf[circ->write_to & (circ->capacity - 1)] = val;
	++circ->write_to;
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

	*pval = circ->buf[circ->read_from & (circ->capacity - 1)];
	++circ->read_from;
	return OK;
}
