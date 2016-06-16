/* convenient, high-level interface for reading & writing storages */

#ifndef BATCH_H
#define BATCH_H

#include <stddef.h>
#include "clock.h"
#include "status.h"
#include "storage.h"

#ifdef __cplusplus
extern "C" {
#endif

status batch_read_records(storage_handle store, size_t copy_size,
						  const identifier *ids, void *values, revision *revs,
						  microsec *times, size_t count);

status batch_write_records(storage_handle store, size_t copy_size,
						   identifier *ids, void *values, size_t count);

status batch_read_changed_records(storage_handle store, size_t copy_size,
								  identifier *ids, void *values, revision *revs,
								  microsec *times, size_t count, microsec wait,
								  q_index *head);

#ifdef __cplusplus
}
#endif

#endif
