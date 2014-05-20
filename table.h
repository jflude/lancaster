/* hash table */

#ifndef TABLE_H
#define TABLE_H

#include "status.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

struct table_t;
typedef struct table_t* table_handle;

typedef void* table_key;
typedef void* table_value;

typedef int (*table_hash_func)(table_key);
typedef boolean (*table_equality_func)(table_key, table_key);
typedef void (*table_destroy_func)(table_key, table_value);
typedef status (*table_iterate_func)(table_key, table_value);

status table_create(table_handle* ptab, size_t sz, table_hash_func h_fn, table_equality_func eq_fn, table_destroy_func dtor_fn);
void table_destroy(table_handle* ptab);

status table_lookup(table_handle tab, table_key key, table_value* pval);
status table_insert(table_handle tab, table_key key, table_value val);
status table_remove(table_handle tab, table_key key);
status table_foreach(table_handle tab, table_iterate_func iter_fn);

#ifdef __cplusplus
}
#endif

#endif
