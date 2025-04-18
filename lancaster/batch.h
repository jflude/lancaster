/*
  Copyright (c)2014-2017 Peak6 Investments, LP.
  Copyright (c)2018-2024 Justin Flude.
  Use of this source code is governed by the COPYING file.
*/

/* convenient, high-level interface for reading & writing storages */

#ifndef BATCH_H
#define BATCH_H

#include <lancaster/clock.h>
#include <lancaster/status.h>
#include <lancaster/storage.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

status batch_read_records(storage_handle store, size_t copy_size,
			  const identifier *ids, void *values, revision *revs,
			  microsec *times, size_t count);

status batch_write_records(storage_handle store, size_t copy_size,
			   const identifier *ids, const void *values,
			   size_t count);

status batch_read_changed_records(storage_handle store, size_t copy_size,
				  identifier *ids, void *values,
				  revision *revs, microsec *times,
				  size_t count, microsec read_timeout,
				  q_index *head);

struct batch_context;
typedef struct batch_context *batch_context_handle;

status batch_read_changed_records2(storage_handle store, size_t copy_size,
                                   identifier *ids, void *values,
                                   revision *revs, microsec *times,
                                   size_t count, microsec read_timeout,
                                   microsec orphan_timeout,
                                   batch_context_handle *pctx);

status batch_context_destroy(batch_context_handle *pctx);

#ifdef __cplusplus
}
#endif

#endif
