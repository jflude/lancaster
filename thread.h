/* portable threading interface */

#ifndef THREAD_H
#define THREAD_H

#include "status.h"

#ifdef __cplusplus
extern "C" {
#endif

struct thread_t;
typedef struct thread_t* thread_handle;

typedef void* (*thread_proc)(thread_handle);

status thread_create(thread_handle* pthr, thread_proc proc, void* param);
void thread_destroy(thread_handle* pthr);

void* thread_get_param(thread_handle thr);
status thread_stop(thread_handle thr, void** presult);

boolean thread_is_running(thread_handle thr);
boolean thread_is_stopping(thread_handle thr);
void thread_has_stopped(thread_handle thr);

#ifdef __cplusplus
}
#endif

#endif
