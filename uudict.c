#include "uudict.h"
#include "error.h"
#include "table.h"
#include "xalloc.h"

struct uudict_t
{
	table_handle uu2id;
	table_handle id2uu;
};

static int uu2id_hash_fn(table_key key)
{
	const char *p = key, *q = p + sizeof(union uuid_t);
	int h = 5381;

	while (p < q)
		h = ((h << 5) + h) ^ *p++;

	return h;
}

static boolean uu2id_eq_fn(table_key key1, table_key key2)
{
	union uuid_t *uu1 = key1, *uu2 = key2;
	return uu1->word.low == uu2->word.low && uu1->word.high == uu2->word.high;
}

static void uu2id_dtor_fn(table_key key, table_value val)
{
	xfree(key);
}

status uudict_create(uudict_handle* puudict, size_t sz)
{
	status st;
	if (!puudict || sz == 0) {
		error_invalid_arg("uudict_create");
		return FAIL;
	}

	*puudict = XMALLOC(struct uudict_t);
	if (!*puudict)
		return NO_MEMORY;

	BZERO(*puudict);

	if (FAILED(st = table_create(&(*puudict)->uu2id, sz, uu2id_hash_fn, uu2id_eq_fn, uu2id_dtor_fn)) ||
		FAILED(st = table_create(&(*puudict)->id2uu, sz, NULL, NULL, NULL))) {
		uudict_destroy(puudict);
		return st;
	}

	return OK;
}

void uudict_destroy(uudict_handle* puudict)
{
	if (!puudict || !*puudict)
		return;

	error_save_last();

	table_destroy(&(*puudict)->id2uu);
	table_destroy(&(*puudict)->uu2id);

	error_restore_last();

	xfree(*puudict);
	*puudict = NULL;
}

status uudict_assoc(uudict_handle uudict, union uuid_t uu, int id)
{
	status st = OK;
	union uuid_t* p = XMALLOC(union uuid_t);
	if (!p)
		return NO_MEMORY;

	*p = uu;

	if (FAILED(st = table_insert(uudict->uu2id, (table_key) p, (table_value) (long) id)))
		return st;

	return table_insert(uudict->id2uu, (table_key) (long) id, (table_value) p);
}

status uudict_get_id(uudict_handle uudict, union uuid_t uu, int* pid)
{
	table_value val;
	status st;

	if (!pid) {
		error_invalid_arg("uudict_get_id");
		return FAIL;
	}

	if (!FAILED(st = table_lookup(uudict->uu2id, (table_key) &uu, &val)) && st)
		*pid = (long) val;

	return st;
}

status uudict_get_uuid(uudict_handle uudict, int id, union uuid_t** ppuu)
{
	if (!ppuu) {
		error_invalid_arg("uudict_get_uuid");
		return FAIL;
	}

	return table_lookup(uudict->id2uu, (table_key) (long) id, (table_value*) (void**) ppuu);
}
