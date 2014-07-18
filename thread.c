#include "thread.h"
#include "error.h"
#include "xalloc.h"
#include <errno.h>
#include <pthread.h>

struct thread_t
{
	pthread_t thr;
	void* param;
	volatile boolean running;
	volatile boolean stopping;
};

status thread_create(thread_handle* pthr, thread_func fn, void* param)
{
	int e;
	if (!pthr || !fn) {
		error_invalid_arg("thread_create");
		return FAIL;
	}

	*pthr = XMALLOC(struct thread_t);
	if (!*pthr)
		return NO_MEMORY;

	(*pthr)->stopping = (*pthr)->running = FALSE;
	(*pthr)->param = param;

	e = pthread_create(&(*pthr)->thr, NULL, (void* (*)(void*)) fn, *pthr);
	if (e) {
		errno = e;
		error_errno("pthread_create");
		thread_destroy(pthr);
		return FAIL;
	}

	(*pthr)->running = TRUE;
	return OK;
}

void thread_destroy(thread_handle* pthr)
{
	if (!pthr || !*pthr)
		return;

	if ((*pthr)->running) {
		error_save_last();
		thread_stop(*pthr, NULL);
		error_restore_last();
	}

	xfree(*pthr);
	*pthr = NULL;
}

void* thread_get_param(thread_handle thr)
{
	return thr->param;
}

status thread_stop(thread_handle thr, void** presult)
{
	int e;
	thr->stopping = TRUE;

	e = pthread_join(thr->thr, presult);
	if (e && e != ESRCH) {
		errno = e;
		error_errno("pthread_join");
		return FAIL;
	}

	thr->running = FALSE;
	return OK;
}

boolean thread_is_running(thread_handle thr)
{
	return thr->running;
}

boolean thread_is_stopping(thread_handle thr)
{
	return thr->stopping;
}

void thread_has_stopped(thread_handle thr)
{
	thr->running = FALSE;
}
