/* in the background, periodically touch a storage */

#ifndef TOUCHER_H
#define TOUCHER_H

#include "clock.h"
#include "storage.h"

#ifdef __cplusplus
extern "C" {
#endif

struct toucher;
typedef struct toucher *toucher_handle;

status toucher_create(toucher_handle *ptouch, storage_handle store,
					  microsec touch_period);
status toucher_destroy(toucher_handle *ptouch);

boolean toucher_is_running(toucher_handle touch);
status toucher_stop(toucher_handle touch);

#ifdef __cplusplus
}
#endif

#endif
