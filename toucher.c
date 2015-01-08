#include "error.h"
#include "thread.h"
#include "toucher.h"
#include "xalloc.h"

struct toucher {
	thread_handle thread;
	storage_handle store;
	microsec period;
};

static void *touch_func(thread_handle thr)
{
	toucher_handle touch = thread_get_param(thr);
	status st = OK;

	while (!thread_is_stopping(thr)) {
		microsec now;
		if (FAILED(st = clock_time(&now)) ||
			FAILED(st = storage_touch(touch->store, now)) ||
			FAILED(st = clock_sleep(touch->period)))
			break;
	}

	return (void *)(long)st;
}

status toucher_create(toucher_handle *ptouch, storage_handle store,
					  microsec touch_period)
{
	status st;
	if (!ptouch || !store || touch_period <= 0)
		return error_invalid_arg("toucher_create");

	*ptouch = XMALLOC(struct toucher);
	if (!*ptouch)
		return NO_MEMORY;

	BZERO(*ptouch);

	(*ptouch)->store = store;
	(*ptouch)->period = touch_period;

	if (FAILED(st = thread_create(&(*ptouch)->thread, touch_func, *ptouch))) {
		error_save_last();
		toucher_destroy(ptouch);
		error_restore_last();
	}

	return st;
}

status toucher_destroy(toucher_handle *ptouch)
{
	status st = OK;
	if (!ptouch || !*ptouch ||
		FAILED(st = thread_destroy(&(*ptouch)->thread)))
		return st;

	XFREE(*ptouch);
	return st;
}

boolean toucher_is_running(toucher_handle touch)
{
	return touch->thread && thread_is_running(touch->thread);
}

status toucher_stop(toucher_handle touch)
{
	void *p;
	status st = thread_stop(touch->thread, &p);
	if (!FAILED(st))
		st = (long)p;

	return st;
}
