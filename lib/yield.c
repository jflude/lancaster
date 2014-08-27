#include "yield.h"
#include <unistd.h>

#ifdef _POSIX_PRIORITY_SCHEDULING

#include "error.h"
#include <sched.h>

status yield(void)
{
	if (sched_yield() == -1) {
		error_errno("sched_yield");
		return FAIL;
	}

	return OK;
}

#else

#include "clock.h"

status yield(void)
{
	return clock_sleep(1);
}

#endif
