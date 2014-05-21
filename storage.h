/* record storage, locking, identity and versioning */

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

struct record_t;
typedef struct record_t* record_handle;

struct storage_t;
typedef struct storage_t* storage_handle;

typedef status (*storage_iterate_func)(record_handle, void*);

struct storage_seq_range_t { long low; long high; };

status storage_create(storage_handle* pstore, int base_id, int max_id, size_t val_size);
void storage_destroy(storage_handle* pstore);

status storage_setup(storage_handle* pstore, void* memory, int base_id, int max_id, size_t val_size);
size_t storage_calc_record_size(size_t val_size);
void storage_zero(storage_handle store);

int storage_get_base_id(storage_handle store);
int storage_get_max_id(storage_handle store);
size_t storage_get_val_size(storage_handle store);

status storage_lookup(storage_handle store, int id, record_handle* prec);
status storage_iterate(storage_handle store, storage_iterate_func iter_fn, record_handle prev, void* param);

int record_get_id(record_handle rec);
void* record_get_val(record_handle rec);

long record_get_seq(record_handle rec);
void record_set_seq(record_handle rec, long seq);

#ifdef __cplusplus
}
#endif

#endif
