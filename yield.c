#include "yield.h"
#include <sched.h>
#include <unistd.h>

void yield(void)
{
	sched_yield();
}

void snooze(unsigned seconds)
{
	while ((seconds = sleep(seconds)) != 0)
		;
}
