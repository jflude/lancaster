#include <errno.h>
#include <stdio.h>
#include "a2i.h"
#include "error.h"

status a2i(const char *text, const char *format, void *pnum)
{
	errno = 0;
	if (sscanf(text, format, pnum) == EOF)
		return errno != 0 ? error_errno("sscanf")
			              : error_msg("invalid number: \"%s\"",
									  INVALID_NUMBER, text);
	return OK;
}
