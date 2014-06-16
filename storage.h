/* record storage, locking, change notification and versioning */

#ifndef STORAGE_H
#define STORAGE_H

#include "spin.h"
#include "status.h"
#include <stddef.h>

#define STORAGE_VERSION 1

#define RECORD_LOCK(r) SPIN_LOCK((spin_lock_t*) (r))
#define RECORD_UNLOCK(r) SPIN_UNLOCK((spin_lock_t*) (r))

#ifdef __cplusplus
extern "C" {
#endif

struct storage_t;
typedef struct storage_t* storage_handle;

struct record_t;
typedef struct record_t* record_handle;

typedef status (*storage_iterate_func)(record_handle, void*);

struct storage_seq_range_t { long low; long high; };

status storage_create(storage_handle* pstore, const char* mmap_file, unsigned q_capacity, int base_id, int max_id, size_t val_size);
status storage_open(storage_handle* pstore, const char* mmap_file);
void storage_destroy(storage_handle* pstore);

boolean storage_is_segment_owner(storage_handle store);
record_handle storage_get_array(storage_handle store);
int storage_get_base_id(storage_handle store);
int storage_get_max_id(storage_handle store);
size_t storage_get_record_size(storage_handle store);
size_t storage_get_value_size(storage_handle store);

size_t storage_get_value_offset(storage_handle store);
const int* storage_get_queue_base_address(storage_handle store);
const unsigned* storage_get_queue_head_address(storage_handle store);

unsigned storage_get_queue_capacity(storage_handle store);
unsigned storage_get_queue_head(storage_handle store);
int storage_read_queue(storage_handle store, unsigned index);
status storage_write_queue(storage_handle store, int id);

status storage_lookup(storage_handle store, int id, record_handle* prec);
status storage_iterate(storage_handle store, storage_iterate_func iter_fn, record_handle prev, void* param);
status storage_reset(storage_handle store);

int record_get_id(record_handle rec);
void* record_get_value(record_handle rec);
long record_get_sequence(record_handle rec);
void record_set_sequence(record_handle rec, long seq);
void* record_get_conflated(record_handle rec);
void record_set_conflated(record_handle rec, void* confl);

#ifdef __cplusplus
}
#endif

#endif
