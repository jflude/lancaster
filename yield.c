#include "yield.h"
#include <sched.h>
#include <unistd.h>

void yield(void)
{
	sched_yield();
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
