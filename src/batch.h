/*
   Copyright (c)2014-2017 Peak6 Investments, LP.  All rights reserved.
   Use of this source code is governed by the LICENSE file.
*/

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
			  const identifier * ids, void *values, revision * revs,
			  microsec * times, size_t count);

status batch_write_records(storage_handle store, size_t copy_size,
			   const identifier * ids, const void *values,
			   size_t count);

status batch_read_changed_records(storage_handle store, size_t copy_size,
				  identifier * ids, void *values,
				  revision * revs, microsec * times,
				  size_t count, microsec timeout,
				  q_index * head);

#ifdef __cplusplus
}
#endif

#endif
