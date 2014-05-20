/* dictionary of string symbols <--> integer identifiers */

#ifndef DICT_H
#define DICT_H

#include "status.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

struct dict_t;
typedef struct dict_t* dict_handle;

status dict_create(dict_handle* pdict, size_t sym2id_sz, size_t id2sym_sz);
void dict_destroy(dict_handle* pdict);

status dict_assoc(dict_handle dict, const char* symbol, int id);

status dict_get_id(dict_handle dict, const char* symbol, int* pid);
status dict_get_symbol(dict_handle dict, int id, const char** psymbol);

#ifdef __cplusplus
}
#endif

#endif
