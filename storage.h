/* record storage, locking, change notification and versioning */

#ifndef STORAGE_H
#define STORAGE_H

#include "status.h"
#include <stddef.h>
#include <sys/types.h>

#define STORAGE_VERSION 1

#ifdef __cplusplus
extern "C" {
#endif

struct storage_t;
typedef struct storage_t* storage_handle;

struct record_t;
typedef struct record_t* record_handle;

typedef status (*storage_iterate_func)(record_handle, void*);

typedef long identifier;
typedef long sequence;

#define SEQUENCE_MAX (1uL >> 1)  /* assumes two's complement */
#define SEQUENCE_MIN (~SEQUENCE_MAX)

struct storage_seq_range_t { sequence low; sequence high; };

status storage_create(storage_handle* pstore, const char* mmap_file, int open_flags, size_t q_capacity,
					  identifier base_id, identifier max_id, size_t val_size);
status storage_open(storage_handle* pstore, const char* mmap_file);
void storage_destroy(storage_handle* pstore);

boolean storage_is_owner(storage_handle store);
pid_t storage_get_owner_pid(storage_handle store);

record_handle storage_get_array(storage_handle store);
identifier storage_get_base_id(storage_handle store);
identifier storage_get_max_id(storage_handle store);
size_t storage_get_record_size(storage_handle store);
size_t storage_get_value_size(storage_handle store);
size_t storage_get_value_offset(storage_handle store);

const identifier* storage_get_queue_base_ref(storage_handle store);
const long* storage_get_queue_head_ref(storage_handle store);
size_t storage_get_queue_capacity(storage_handle store);
long storage_get_queue_head(storage_handle store);
identifier storage_read_queue(storage_handle store, long index);
status storage_write_queue(storage_handle store, identifier id);

time_t storage_get_send_recv_time(storage_handle store);
void storage_set_send_recv_time(storage_handle store, time_t when);

status storage_get_id(storage_handle store, record_handle rec, identifier* pident);
status storage_get_record(storage_handle store, identifier id, record_handle* prec);
status storage_iterate(storage_handle store, storage_iterate_func iter_fn, record_handle prev, void* param);
status storage_sync(storage_handle store);
status storage_reset(storage_handle store);

identifier storage_get_high_water_id(storage_handle store);
status storage_set_high_water_id(storage_handle store, identifier id);

void* record_get_value(record_handle rec);
sequence record_get_sequence(record_handle rec);
void record_set_sequence(record_handle rec, sequence seq);
void* record_get_conflated(record_handle rec);
void record_set_conflated(record_handle rec, void* confl);

sequence record_read_lock(record_handle rec);
sequence record_write_lock(record_handle rec);

#ifdef __cplusplus
}
#endif

#endif
