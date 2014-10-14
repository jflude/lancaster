#include "table.h"
#include "error.h"
#include "xalloc.h"

struct chain {
	table_key key;
	table_value val;
	struct chain *next;
};

struct table {
	struct chain **array;
	size_t size;
	table_hash_func h_fn;
	table_equality_func eq_fn;
	table_destroy_func dtor_fn;
};

status table_create(table_handle *ptab, size_t tab_sz, table_hash_func h_fn,
					table_equality_func eq_fn, table_destroy_func dtor_fn)
{
	if (!ptab || tab_sz == 0)
		return error_invalid_arg("table_create");

	*ptab = XMALLOC(struct table);
	if (!*ptab)
		return NO_MEMORY;

	(*ptab)->size = tab_sz;
	(*ptab)->h_fn = h_fn;
	(*ptab)->eq_fn = eq_fn;
	(*ptab)->dtor_fn = dtor_fn;

	(*ptab)->array = xcalloc(tab_sz, sizeof(struct chain_t *));
	if (!(*ptab)->array) {
		table_destroy(ptab);
		return NO_MEMORY;
	}

	return OK;
}

status table_destroy(table_handle *ptab)
{
	size_t i;
	if (!ptab || !*ptab)
		return OK;

	for (i = 0; i < (*ptab)->size; ++i) {
		struct chain *c = (*ptab)->array[i];
		while (c) {
			struct chain *next;
			if ((*ptab)->dtor_fn)
				(*ptab)->dtor_fn(c->key, c->val);

			next = c->next;
			xfree(c);
			c = next;
		}
	}

	XFREE((*ptab)->array);
	XFREE(*ptab);
	return OK;
}

status table_lookup(table_handle tab, table_key key, table_value *pval)
{
	struct chain *c;
	size_t hash;
	if (!pval)
		return error_invalid_arg("table_lookup");

	hash = (tab->h_fn ? tab->h_fn(key) : (long)key) % tab->size;

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
	struct chain *c = NULL;
	size_t hash = (tab->h_fn ? tab->h_fn(key) : (long)key) % tab->size;

	if (!tab->array[hash]) {
		c = XMALLOC(struct chain);
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

		c = XMALLOC(struct chain);
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
	struct chain *c;
	struct chain *prev = NULL;
	size_t hash = (tab->h_fn ? tab->h_fn(key) : (long)key) % tab->size;

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

		XFREE(c);
		return OK;
	}

	return NOT_FOUND;
}

status table_iterate(table_handle tab, table_iterate_func iter_fn)
{
	status st = TRUE;
	size_t i;
	if (!iter_fn)
		return error_invalid_arg("table_iterate");

	for (i = 0; i < tab->size; ++i) {
		struct chain *c;
		for (c = tab->array[i]; c; c = c->next) {
			st = iter_fn(c->key, c->val);
			if (FAILED(st) || !st)
				return st;
		}
	}

	return st;
}
