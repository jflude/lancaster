/*
  Copyright (c)2014-2017 Peak6 Investments, LP.
  Copyright (c)2018-2024 Justin Flude.
  Use of this source code is governed by the COPYING file.
*/

#include <lancaster/error.h>
#include <lancaster/thread.h>
#include <lancaster/xalloc.h>
#include <errno.h>
#include <pthread.h>

struct thread {
    pthread_t sys_thr;
    thread_func func;
    void *param;
    void *property;
    volatile boolean running;
    volatile boolean stopping;
};

static void *wrapper_fn(void *param)
{
    thread_handle thr = param;
    void *result = thr->func(thr);
    thr->running = FALSE;
    return result;
}

status thread_create(thread_handle *pthr, thread_func fn, void *param)
{
    int e;
    if (!pthr || !fn)
	return error_invalid_arg("thread_create");

    *pthr = XMALLOC(struct thread);
    if (!*pthr)
	return NO_MEMORY;

    BZERO(*pthr);

    (*pthr)->func = fn;
    (*pthr)->param = param;

    e = pthread_create(&(*pthr)->sys_thr, NULL, wrapper_fn, *pthr);
    if (e) {
	error_save_last();
	thread_destroy(pthr);
	error_restore_last();

	errno = e;
	return error_errno("thread_create: pthread_create");
    }

    (*pthr)->running = TRUE;
    return OK;
}

status thread_destroy(thread_handle *pthr)
{
    status st = OK;
    if (!pthr || !*pthr)
	return st;

    if ((*pthr)->running)
	st = thread_stop(*pthr, NULL);

    XFREE(*pthr);
    return st;
}

void *thread_get_param(thread_handle thr)
{
    return thr->param;
}

void *thread_get_property(thread_handle thr)
{
    return thr->property;
}

void thread_set_property(thread_handle thr, void *prop)
{
    thr->property = prop;
}

status thread_stop(thread_handle thr, void **presult)
{
    if (thr->sys_thr) {
	int e;
	thr->stopping = TRUE;

	e = pthread_join(thr->sys_thr, presult);
	if (e && e != ESRCH) {
	    errno = e;
	    return error_errno("thread_stop: pthread_join");
	}
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
