/*
  Copyright (c)2014-2017 Peak6 Investments, LP.
  Use of this source code is governed by the COPYING file.
*/

#include <lancaster/error.h>
#include <lancaster/spin.h>
#include <lancaster/thread.h>
#include <lancaster/toucher.h>
#include <lancaster/xalloc.h>

struct target {
    storage_handle store;
    struct target *next;
};

struct toucher {
    thread_handle thr;
    microsec period;
    struct target *targets;
    volatile spin_lock lock;
};

static void *touch_func(thread_handle thr)
{
    toucher_handle touch = thread_get_param(thr);
    status st = OK;

    while (!thread_is_stopping(thr)) {
	microsec now;
	struct target *t;

	if (FAILED(st = spin_write_lock(&touch->lock, NULL)))
	    break;

	if (!FAILED(st = clock_time(&now)))
	    for (t = touch->targets; t; t = t->next)
		if (FAILED(st = storage_touch(t->store, now)))
		    break;

	spin_unlock(&touch->lock, 0);
	if (FAILED(st) || FAILED(st = clock_sleep(touch->period)))
	    break;
    }

    return (void *)(long)st;
}

status toucher_create(toucher_handle *ptouch, microsec touch_period_usec)
{
    status st = OK;
    if (!ptouch || touch_period_usec < 0)
	return error_invalid_arg("toucher_create");

    *ptouch = XMALLOC(struct toucher);
    if (!*ptouch)
	return NO_MEMORY;

    BZERO(*ptouch);
    spin_create(&(*ptouch)->lock);

    (*ptouch)->period = touch_period_usec;

    if (touch_period_usec > 0 &&
	FAILED(st = thread_create(&(*ptouch)->thr, touch_func, *ptouch))) {
	error_save_last();
	toucher_destroy(ptouch);
	error_restore_last();
    }

    return st;
}

status toucher_destroy(toucher_handle *ptouch)
{
    status st = OK;
    struct target *t;

    if (!ptouch || !*ptouch || FAILED(st = thread_destroy(&(*ptouch)->thr)))
	return st;

    for (t = (*ptouch)->targets; t;) {
	struct target *next = t->next;
	xfree(t);
	t = next;
    }

    XFREE(*ptouch);
    return st;
}

boolean toucher_is_running(toucher_handle touch)
{
    return touch->thr && thread_is_running(touch->thr);
}

status toucher_stop(toucher_handle touch)
{
    void *p;
    status st = thread_stop(touch->thr, &p);
    if (!FAILED(st))
	st = (long)p;

    return st;
}

status toucher_add_storage(toucher_handle touch, storage_handle store)
{
    struct target *t;
    status st;

    if (!store)
	return error_invalid_arg("toucher_add_storage");

    if (FAILED(st = spin_write_lock(&touch->lock, NULL)))
	return st;

    for (t = touch->targets; t; t = t->next)
	if (t->store == store) {
	    spin_unlock(&touch->lock, 0);
	    return OK;
	}

    t = XMALLOC(struct target);
    if (!t) {
	spin_unlock(&touch->lock, 0);
	return NO_MEMORY;
    }

    t->store = store;
    t->next = touch->targets;
    touch->targets = t;

    spin_unlock(&touch->lock, 0);
    return OK;
}

status toucher_remove_storage(toucher_handle touch, storage_handle store)
{
    struct target *t;
    struct target **prev;
    status st;

    if (!store)
	return error_invalid_arg("toucher_remove_storage");

    if (FAILED(st = spin_write_lock(&touch->lock, NULL)))
	return st;

    st = NOT_FOUND;
    for (prev = &touch->targets, t = touch->targets;
	 t; prev = &t->next, t = t->next)
	if (t->store == store) {
	    *prev = t->next;
	    xfree(t);

	    st = OK;
	    break;
	}

    spin_unlock(&touch->lock, 0);
    return st;
}
