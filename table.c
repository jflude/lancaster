#include "table.h"
#include "error.h"
#include "xalloc.h"

struct chain_t
{
	table_key key;
	table_value val;
	struct chain_t* next;
};

struct table_t
{
	struct chain_t** array;
	size_t size;
	table_hash_func h_fn;
	table_equality_func eq_fn;
	table_destroy_func dtor_fn;
};

status table_create(table_handle* ptab, size_t sz, table_hash_func h_fn, table_equality_func eq_fn, table_destroy_func dtor_fn)
{
	if (!ptab || sz == 0) {
		error_invalid_arg("table_create");
		return FAIL;
	}

	*ptab = XMALLOC(struct table_t);
	if (!*ptab)
		return NO_MEMORY;

	(*ptab)->size = sz;
	(*ptab)->h_fn = h_fn;
	(*ptab)->eq_fn = eq_fn;
	(*ptab)->dtor_fn = dtor_fn;

	(*ptab)->array = xcalloc(sz, sizeof(struct chain_t*));
	if (!(*ptab)->array) {
		table_destroy(ptab);
		return NO_MEMORY;
	}

	return OK;
}

void table_destroy(table_handle* ptab)
{
	size_t i;
	if (!ptab || !*ptab)
		return;

	for (i = 0; i < (*ptab)->size; ++i) {
		struct chain_t* c = (*ptab)->array[i];
		while (c) {
			struct chain_t* next;
			if ((*ptab)->dtor_fn)
				(*ptab)->dtor_fn(c->key, c->val);

			next = c->next;
			xfree(c);
			c = next;
		}
	}

	xfree((*ptab)->array);
	xfree(*ptab);
	*ptab = NULL;
}

status table_lookup(table_handle tab, table_key key, table_value* pval)
{
	struct chain_t* c;
	size_t hash;
	if (!pval) {
		error_invalid_arg("table_lookup");
		return FAIL;
	}

	hash = (tab->h_fn ? tab->h_fn(key) : (long) key) % tab->size;

	for (c = tab->array[hash]; c; c = c->next) {
		if (tab->eq_fn) {
			if (!tab->eq_fn(c->key, key))
				continue;
		} else if (c->key != key)
			continue;

		*pval = c->val;
		return TRUE;
	}

	return FALSE;
}

status table_insert(table_handle tab, table_key key, table_value val)
{
	struct chain_t* c = NULL;
	size_t hash = (tab->h_fn ? tab->h_fn(key) : (long) key) % tab->size;

	if (!tab->array[hash]) {
		c = XMALLOC(struct chain_t);
		if (!c)
			return NO_MEMORY;

		c->next = NULL;
		tab->array[hash] = c;
	}

	if (!c) {
		for (c = tab->array[hash]; c; c = c->next) {
			if (tab->eq_fn) {
				if (!tab->eq_fn(c->key, key))
					continue;
			} else if (c->key != key)
				continue;

			c->val = val;
			return OK;
		}

		c = XMALLOC(struct chain_t);
		if (!c)
			return NO_MEMORY;

		c->next = tab->array[hash];
		tab->array[hash] = c;
	}

	c->key = key;
	c->val = val;
	return OK;
}

status table_remove(table_handle tab, table_key key)
{
	struct chain_t* c;
	struct chain_t* prev = NULL;
	size_t hash = (tab->h_fn ? tab->h_fn(key) : (long) key) % tab->size;

	for (c = tab->array[hash]; c; prev = c, c = c->next) {
		if (tab->eq_fn) {
			if (!tab->eq_fn(c->key, key))
				continue;
		} else if (c->key != key)
			continue;

		if (tab->dtor_fn)
			tab->dtor_fn(c->key, c->val);

		if (prev)
			prev->next = c->next;
		else
			tab->array[hash] = c->next;

		xfree(c);
		return OK;
	}

	return FAIL;
}

status table_iterate(table_handle tab, table_iterate_func iter_fn)
{
	status st = TRUE;
	size_t i;
	if (!iter_fn) {
		error_invalid_arg("table_iterate");
		return FAIL;
	}

	for (i = 0; i < tab->size; ++i) {
		struct chain_t* c;
		for (c = tab->array[i]; c; c = c->next) {
			st = iter_fn(c->key, c->val);
			if (FAILED(st) || !st)
				return st;
		}
	}

	return st;
}
