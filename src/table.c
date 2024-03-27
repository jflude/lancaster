/*
  Copyright (c)2014-2017 Peak6 Investments, LP.
  Copyright (c)2018-2024 Justin Flude.
  Use of this source code is governed by the COPYING file.
*/

#include <lancaster/error.h>
#include <lancaster/table.h>
#include <lancaster/xalloc.h>

struct chain {
    table_key key;
    table_value val;
    struct chain *next;
};

struct table {
    struct chain **array;
    size_t mask;
    table_hash_func h_fn;
    table_equality_func eq_fn;
    table_destroy_func dtor_fn;
};

status table_create(table_handle *ptab, size_t tab_capacity,
		    table_hash_func h_fn, table_equality_func eq_fn,
		    table_destroy_func dtor_fn)
{
    if (!ptab)
	return error_invalid_arg("table_create");

    /* NB. tab_capacity must be a power of 2 */
    if (tab_capacity == 0 || (tab_capacity & (tab_capacity - 1)) != 0)
	return error_msg(INVALID_CAPACITY,
			 "table_create: invalid table capacity");

    *ptab = XMALLOC(struct table);
    if (!*ptab)
	return NO_MEMORY;

    (*ptab)->mask = tab_capacity - 1;
    (*ptab)->h_fn = h_fn;
    (*ptab)->eq_fn = eq_fn;
    (*ptab)->dtor_fn = dtor_fn;

    (*ptab)->array = xcalloc(tab_capacity, sizeof(struct chain_t *));
    if (!(*ptab)->array) {
	error_save_last();
	table_destroy(ptab);
	error_restore_last();
	return NO_MEMORY;
    }

    return OK;
}

status table_destroy(table_handle *ptab)
{
    size_t i;
    if (!ptab || !*ptab)
	return OK;

    for (i = 0; i <= (*ptab)->mask; ++i) {
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

    xfree((*ptab)->array);
    XFREE(*ptab);
    return OK;
}

status table_lookup(table_handle tab, table_key key, table_value *pval)
{
    struct chain *c;
    size_t hash;
    if (!pval)
	return error_invalid_arg("table_lookup");

    hash = (tab->h_fn ? tab->h_fn(key) : (long)key) & tab->mask;

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
    size_t hash = (tab->h_fn ? tab->h_fn(key) : (long)key) & tab->mask;

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
    size_t hash = (tab->h_fn ? tab->h_fn(key) : (long)key) & tab->mask;

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

    return NOT_FOUND;
}

status table_iterate(table_handle tab, table_iterate_func iter_fn)
{
    status st = TRUE;
    size_t i;
    if (!iter_fn)
	return error_invalid_arg("table_iterate");

    for (i = 0; i <= tab->mask; ++i) {
	struct chain *c;
	for (c = tab->array[i]; c; c = c->next) {
	    st = iter_fn(c->key, c->val);
	    if (FAILED(st) || !st)
		return st;
	}
    }

    return st;
}
