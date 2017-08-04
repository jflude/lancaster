/*
   Copyright (c)2014-2017 Peak6 Investments, LP.  All rights reserved.
   Use of this source code is governed by the LICENSE file.
*/

/* dictionary of strings <--> storage identifiers */

#ifndef DICT_H
#define DICT_H

#include <lancaster/status.h>
#include <lancaster/storage.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

struct dict;
typedef struct dict *dict_handle;

status dict_create(dict_handle *pdict, size_t dict_capacity);
status dict_destroy(dict_handle *pdict);

status dict_assoc(dict_handle dict, const char *symbol, identifier id);

status dict_get_id(dict_handle dict, const char *symbol, identifier *pident);
status dict_get_symbol(dict_handle dict, identifier id, const char **psymbol);

#ifdef __cplusplus
}
#endif

#endif
