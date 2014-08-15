/* record storage, locking, change notification and versioning */

#ifndef STORAGE_H
#define STORAGE_H

#include "clock.h"
#include "status.h"
#include <limits.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define STORAGE_VERSION 1

struct storage_t;
typedef struct storage_t* storage_handle;

struct record_t;
typedef struct record_t* record_handle;

typedef status (*storage_iterate_func)(record_handle, void*);

typedef long identifier;
typedef long queue_index;
typedef long version;

#define VERSION_MIN LONG_MIN
#define VERSION_MAX LONG_MAX

status storage_create(storage_handle* pstore, const char* mmap_file, int open_flags,
					  identifier base_id, identifier max_id, size_t value_size, size_t q_capacity);
status storage_open(storage_handle* pstore, const char* mmap_file, int open_flags);
void storage_destroy(storage_handle* pstore);

boolean storage_is_read_only(storage_handle store);

record_handle storage_get_array(storage_handle store);
identifier storage_get_base_id(storage_handle store);
identifier storage_get_max_id(storage_handle store);
size_t storage_get_record_size(storage_handle store);
size_t storage_get_value_size(storage_handle store);
size_t storage_get_value_offset(storage_handle store);

const char* storage_get_file(storage_handle store);
const char* storage_get_description(storage_handle store);
status storage_set_description(storage_handle store, const char* desc);

microsec_t storage_get_created_time(storage_handle store);
microsec_t storage_get_changed_time(storage_handle store);
status storage_set_changed_time(storage_handle store, microsec_t when);

const identifier* storage_get_queue_base_ref(storage_handle store);
const queue_index* storage_get_queue_head_ref(storage_handle store);
size_t storage_get_queue_capacity(storage_handle store);
queue_index storage_get_queue_head(storage_handle store);
identifier storage_read_queue(storage_handle store, queue_index index);
status storage_write_queue(storage_handle store, identifier id);

status storage_get_id(storage_handle store, record_handle rec, identifier* pident);
status storage_get_record(storage_handle store, identifier id, record_handle* prec);
status storage_iterate(storage_handle store, storage_iterate_func iter_fn, record_handle prev, void* param);
status storage_sync(storage_handle store);
status storage_reset(storage_handle store);

void* record_get_value_ref(record_handle rec);
version record_get_version(record_handle rec);
void record_set_version(record_handle rec, version ver);
version record_read_lock(record_handle rec);
version record_write_lock(record_handle rec);

#ifdef __cplusplus
}
#endif

#endif
