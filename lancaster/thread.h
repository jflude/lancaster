/*
  Copyright (c)2014-2018 Peak6 Investments, LP.  All rights reserved.
  Use of this source code is governed by the COPYING file.
*/

/* portable threading interface */

#ifndef THREAD_H
#define THREAD_H

#include <lancaster/status.h>

#ifdef __cplusplus
extern "C" {
#endif

struct thread;
typedef struct thread *thread_handle;

typedef void *(*thread_func)(thread_handle);

status thread_create(thread_handle *pthr, thread_func fn, void *param);
status thread_destroy(thread_handle *pthr);

void *thread_get_param(thread_handle thr);
void *thread_get_property(thread_handle thr);
void thread_set_property(thread_handle thr, void *prop);

status thread_stop(thread_handle thr, void **presult);

boolean thread_is_running(thread_handle thr);
boolean thread_is_stopping(thread_handle thr);

#ifdef __cplusplus
}
#endif

#endif
