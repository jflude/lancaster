#include "error.h"
#include "twist.h"
#include "xalloc.h"

struct twist {
	int index;
	unsigned buffer[624];
};

status twist_create(twist_handle *ptwist)
{
	if (!ptwist)
		return error_invalid_arg("twist_create");

	*ptwist = XMALLOC(struct twist);
	if (!*ptwist)
		return NO_MEMORY;

	return OK;
}

status twist_destroy(twist_handle *ptwist)
{
	if (ptwist && *ptwist)
		XFREE(*ptwist);

	return OK;
}

void twist_seed(twist_handle twist, unsigned seed)
{
	int i;
	twist->index = 0;
	twist->buffer[0] = seed;

	for (i = 1; i < 624; ++i)
		twist->buffer[i] = 1812433253 *
			(twist->buffer[i - 1] ^ (twist->buffer[i - 1] >> 30)) + i;
}

unsigned twist_rand(twist_handle twist)
{
	int i = twist->index, i2 = twist->index + 1, j = twist->index + 397;
	unsigned s, r;

	if (i2 >= 624)
		i2 = 0;

	if (j >= 624)
		j -= 624;

	s = (twist->buffer[i] & 0x80000000) | (twist->buffer[i2] & 0x7FFFFFFF);
	r = twist->buffer[j] ^ (s >> 1) ^ ((s & 1) * 0x9908B0DF);

	twist->buffer[twist->index] = r;
	twist->index = i2;

	r ^= (r >> 11);
	r ^= (r << 7) & 0x9D2C5680uL;
	r ^= (r << 15) & 0xEFC60000uL;
	r ^= (r >> 18);
	return r;
}
