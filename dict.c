#include "dict.h"
#include "error.h"
#include "table.h"
#include "xalloc.h"

struct dict_t
{
	table_handle sym2id;
	table_handle id2sym;
	boolean use_sym2id;
	boolean use_id2sym;
};

static int dict_sym2id_hash_fn(table_key key)
{
	const char* p = key;
	int c, h = 5381;

	while ((c = *p++) != 0)
		h = ((h << 5) + h) ^ c;

	return h;
}

static boolean dict_sym2id_eq_fn(table_key key1, table_key key2)
{
	return strcmp(key1, key2) == 0;
}

static void dict_sym2id_dtor_fn(table_key key, table_value val)
{
	xfree(key);
}

status dict_create(dict_handle* pdict, size_t sym2id_sz, size_t id2sym_sz)
{
	status st;
	if (!pdict || (sym2id_sz == 0 && id2sym_sz == 0)) {
		error_invalid_arg("dict_create");
		return FAIL;
	}

	*pdict = XMALLOC(struct dict_t);
	if (!*pdict)
		return NO_MEMORY;

	BZERO(*pdict);

	(*pdict)->use_sym2id = (sym2id_sz > 0);
	(*pdict)->use_id2sym = (id2sym_sz > 0);

	if (((*pdict)->use_sym2id &&
		 FAILED(st = table_create(&(*pdict)->sym2id, sym2id_sz, dict_sym2id_hash_fn, dict_sym2id_eq_fn, dict_sym2id_dtor_fn))) ||
		((*pdict)->use_id2sym &&
		 FAILED(st = table_create(&(*pdict)->id2sym, id2sym_sz, NULL, NULL, NULL)))) {
		dict_destroy(pdict);
		return st;
	}

	return OK;
}

void dict_destroy(dict_handle* pdict)
{
	if (!pdict || !*pdict)
		return;

	error_save_last();

	table_destroy(&(*pdict)->id2sym);
	table_destroy(&(*pdict)->sym2id);

	error_restore_last();

	xfree(*pdict);
	*pdict = NULL;
}

status dict_assoc(dict_handle dict, const char* symbol, int id)
{
	status st;
	const char* s;

	if (!symbol) {
		error_invalid_arg("dict_assoc");
		return FAIL;
	}

	s = xstrdup(symbol);
	if (!s)
		return NO_MEMORY;

	if (dict->use_sym2id && FAILED(st = table_insert(dict->sym2id, (table_key) s, (table_value) (long) id)))
		return st;

	return dict->use_id2sym ? table_insert(dict->id2sym, (table_key) (long) id, (table_value) s) : OK;
}

status dict_get_id(dict_handle dict, const char* symbol, int* pval)
{
	table_value val;
	status st;

	if (!symbol || !pval) {
		error_invalid_arg("dict_get_id");
		return FAIL;
	}

	if (!dict->use_sym2id)
		return FALSE;

	if (!FAILED(st = table_lookup(dict->sym2id, (table_key) symbol, &val)) && st)
		*pval = (long) val;

	return st;
}

status dict_get_symbol(dict_handle dict, int id, const char** psymbol)
{
	if (!psymbol) {
		error_invalid_arg("dict_get_symbol");
		return FAIL;
	}

	if (dict->use_id2sym)
		return FALSE;

	return table_lookup(dict->id2sym, (table_key) (long) id, (void**) psymbol);
}
