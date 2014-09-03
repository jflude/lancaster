#include "uudict.h"
#include "error.h"
#include "table.h"
#include "xalloc.h"

struct uudict
{
	table_handle uu2id;
	table_handle id2uu;
};

static int uu2id_hash_fn(table_key key)
{
	union uuid* uu = key;
	long n = uu->word.low ^ uu->word.high;
	return ((int) (n >> 32)) ^ (int) n;
}

static boolean uu2id_eq_fn(table_key key1, table_key key2)
{
	union uuid *uu1 = key1, *uu2 = key2;
	return uu1->word.low == uu2->word.low && uu1->word.high == uu2->word.high;
}

static void uu2id_dtor_fn(table_key key, table_value val)
{
	(void) val;
	xfree(key);
}

status uudict_create(uudict_handle* puudict, size_t dict_sz)
{
	status st;
	if (!puudict || dict_sz == 0)
		return error_invalid_arg("uudict_create");

	*puudict = XMALLOC(struct uudict);
	if (!*puudict)
		return NO_MEMORY;

	BZERO(*puudict);

	if (FAILED(st = table_create(&(*puudict)->uu2id, dict_sz,
								 uu2id_hash_fn, uu2id_eq_fn, uu2id_dtor_fn)) ||
		FAILED(st = table_create(&(*puudict)->id2uu, dict_sz,
								 NULL, NULL, NULL))) {
		uudict_destroy(puudict);
		return st;
	}

	return OK;
}

status uudict_destroy(uudict_handle* puudict)
{
	if (!puudict || !*puudict)
		return OK;

	table_destroy(&(*puudict)->id2uu);
	table_destroy(&(*puudict)->uu2id);

	xfree(*puudict);
	*puudict = NULL;
	return OK;
}

status uudict_assoc(uudict_handle uudict, union uuid uu, identifier id)
{
	status st = OK;
	union uuid* p = XMALLOC(union uuid);
	if (!p)
		return NO_MEMORY;

	*p = uu;

	if (FAILED(st = table_insert(uudict->uu2id, (table_key) p,
								 (table_value) (long) id)))
		return st;

	return table_insert(uudict->id2uu, (table_key) (long) id, (table_value) p);
}

status uudict_get_id(uudict_handle uudict, union uuid uu, identifier* pident)
{
	table_value val;
	status st;

	if (!pident)
		return error_invalid_arg("uudict_get_id");

	if (!FAILED(st = table_lookup(uudict->uu2id, (table_key) &uu, &val)) && st)
		*pident = (long) val;

	return st;
}

status uudict_get_uuid(uudict_handle uudict, identifier id, union uuid** ppuu)
{
	if (!ppuu)
		return error_invalid_arg("uudict_get_uuid");

	return table_lookup(uudict->id2uu, (table_key) (long) id,
						(table_value*) (void**) ppuu);
}
