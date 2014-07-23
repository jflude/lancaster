#include "yield.h"
#include <sched.h>
#include <unistd.h>

void yield(void)
{
#ifdef _POSIX_PRIORITY_SCHEDULING
	sched_yield();
#else
	snooze();
#endif
}

void snooze(void)
{
	usleep(1);
}

void slumber(unsigned seconds)
{
	while ((seconds = sleep(seconds)) != 0)
		/* empty */ ;
}
