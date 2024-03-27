/*
  Copyright (c)2014-2017 Peak6 Investments, LP.
  Copyright (c)2018-2024 Justin Flude.
  Use of this source code is governed by the COPYING file.
*/

/* modify the attributes of a storage */

#include <lancaster/a2i.h>
#include <lancaster/error.h>
#include <lancaster/storage.h>
#include <lancaster/version.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

typedef identifier (*id_attr_func)(storage_handle);
typedef size_t (*sz_attr_func)(storage_handle);

static storage_handle old_store, new_store;

static void show_syntax(void)
{
    fprintf(stderr, "Syntax: %s [-v] [-L] STORAGE-FILE NEW-STORAGE-FILE "
	    "NEW-BASE-ID NEW-MAX-ID NEW-VALUE-SIZE NEW-PROPERTY-SIZE "
	    "NEW-QUEUE-CAPACITY\n", error_get_program_name());

    exit(-SYNTAX_ERROR);
}

static identifier parse_id(const char *text, id_attr_func fn)
{
    int64_t n;
    if (*text == '=')
	return fn(old_store);

    if (FAILED(a2i(text, "%" PRId64, &n)))
	error_report_fatal();

    return n;
}

static size_t parse_sz(const char *text, sz_attr_func fn)
{
    unsigned long n;
    if (*text == '=')
	return fn(old_store);

    if (FAILED(a2i(text, "%lu", &n)))
	error_report_fatal();

    return n;
}

int main(int argc, char *argv[])
{
    identifier new_base_id, new_max_id;
    size_t new_val_size, new_prop_size, new_q_capacity;
    const char *new_file;
    int opt;

    error_set_program_name(argv[0]);

    while ((opt = getopt(argc, argv, "Lv")) != -1)
	switch (opt) {
	case 'L':
	    error_with_timestamp(TRUE);
	    break;
	case 'v':
	    show_version("grower");
	    /* fall through */
	default:
	    show_syntax();
	}

    if ((argc - optind) != 7)
	show_syntax();

    if (FAILED(storage_open(&old_store, argv[optind++], O_RDONLY)))
	error_report_fatal();

    new_file = argv[optind++];
    new_base_id = parse_id(argv[optind++], storage_get_base_id);
    new_max_id = parse_id(argv[optind++], storage_get_max_id);
    new_val_size = parse_sz(argv[optind++], storage_get_value_size);
    new_prop_size = parse_sz(argv[optind++], storage_get_property_size);
    new_q_capacity = parse_sz(argv[optind++], storage_get_queue_capacity);

    if (FAILED(storage_grow(old_store, &new_store, new_file, O_CREAT,
			    new_base_id, new_max_id, new_val_size,
			    new_prop_size, new_q_capacity)) ||
	FAILED(storage_destroy(&old_store)) ||
	FAILED(storage_destroy(&new_store)))
	error_report_fatal();

    return 0;
}
