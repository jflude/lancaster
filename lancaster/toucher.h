/*
   Copyright (c)2014-2017 Peak6 Investments, LP.  All rights reserved.
   Use of this source code is governed by the LICENSE file.
*/

/* in the background, periodically touch a storage */

#ifndef TOUCHER_H
#define TOUCHER_H

#include <lancaster/clock.h>
#include <lancaster/storage.h>

#ifdef __cplusplus
extern "C" {
#endif

struct toucher;
typedef struct toucher *toucher_handle;

status toucher_create(toucher_handle *ptouch, microsec touch_period_usec);
status toucher_destroy(toucher_handle *ptouch);

boolean toucher_is_running(toucher_handle touch);
status toucher_stop(toucher_handle touch);

status toucher_add_storage(toucher_handle touch, storage_handle store);
status toucher_remove_storage(toucher_handle touch, storage_handle store);

#ifdef __cplusplus
}
#endif

#endif
