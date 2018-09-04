/*
  Copyright (c)2014-2018 Peak6 Investments, LP.  All rights reserved.
  Use of this source code is governed by the COPYING file.
*/

#include <lancaster/dict.h>
#include <lancaster/error.h>
#include <lancaster/table.h>
#include <lancaster/xalloc.h>
#include <string.h>

struct dict {
    table_handle s2id;
    table_handle id2s;
};

static int s2id_hash_fn(table_key key)
{
    const char *p = key;
    int c, h = 5381;

    while ((c = *p++) != '\0')
	h = ((h << 5) + h) ^ c;

    return h;
}

static boolean s2id_eq_fn(table_key key1, table_key key2)
{
    return strcmp(key1, key2) == 0;
}

static void s2id_dtor_fn(table_key key, table_value val)
{
    (void)val;
    XFREE(key);
}

status dict_create(dict_handle *pdict, size_t dict_capacity)
{
    status st;
    if (!pdict)
	return error_invalid_arg("dict_create");

    *pdict = XMALLOC(struct dict);
    if (!*pdict)
	return NO_MEMORY;

    BZERO(*pdict);

    if (FAILED(st = table_create(&(*pdict)->s2id, dict_capacity,
				 s2id_hash_fn, s2id_eq_fn, s2id_dtor_fn)) ||
	FAILED(st = table_create(&(*pdict)->id2s, dict_capacity,
				 NULL, NULL, NULL))) {
	error_save_last();
	dict_destroy(pdict);
	error_restore_last();
	return st;
    }

    return OK;
}

status dict_destroy(dict_handle *pdict)
{
    status st = OK;
    if (!pdict || !*pdict ||
	FAILED(st = table_destroy(&(*pdict)->id2s)) ||
	FAILED(st = table_destroy(&(*pdict)->s2id)))
	return st;

    XFREE(*pdict);
    return st;
}

status dict_assoc(dict_handle dict, const char *symbol, identifier id)
{
    status st;
    const char *p;

    if (!symbol)
	return error_invalid_arg("dict_assoc");

    p = xstrdup(symbol);
    if (!p)
	return NO_MEMORY;

    if (FAILED(st = table_insert(dict->s2id, (table_key)p,
				 (table_value)(long)id)))
	return st;

    return table_insert(dict->id2s, (table_key)(long)id, (table_value)p);
}

status dict_get_id(dict_handle dict, const char *symbol, identifier *pident)
{
    table_value val;
    status st;

    if (!symbol || !pident)
	return error_invalid_arg("dict_get_id");

    if (!FAILED(st = table_lookup(dict->s2id, (table_key)symbol, &val)) && st)
	*pident = (long)val;

    return st;
}

status dict_get_symbol(dict_handle dict, identifier id, const char **psymbol)
{
    if (!psymbol)
	return error_invalid_arg("dict_get_symbol");

    return table_lookup(dict->id2s, (table_key)(long)id,
			(table_value *)(void **)psymbol);
}
