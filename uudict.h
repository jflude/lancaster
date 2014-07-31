/* dictionary of UUIDs <--> storage identifiers */

#ifndef UUDICT_H
#define UUDICT_H

#include "status.h"
#include "storage.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

union uuid_t
{
	char byte[16];
	struct { long low; long high; } word;
};

struct uudict_t;
typedef struct uudict_t* uudict_handle;

status uudict_create(uudict_handle* puudict, size_t dict_sz);
void uudict_destroy(uudict_handle* puudict);

status uudict_assoc(uudict_handle uudict, union uuid_t uu, identifier id);

status uudict_get_id(uudict_handle uudict, union uuid_t uu, identifier* pident);
status uudict_get_uuid(uudict_handle uudict, identifier id, union uuid_t** ppuu);

#ifdef __cplusplus
}
#endif

#endif
