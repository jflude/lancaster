/*
   Copyright (C)2014-2016 Peak6 Investments, LP.  All rights reserved.
   Use of this source code is governed by the LICENSE file.
*/

/* modify the attributes of a storage */

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "a2i.h"
#include "error.h"
#include "storage.h"
#include "version.h"

typedef long (*attr_func)(storage_handle);

static storage_handle old_store, new_store;

static void show_syntax(void)
{
	fprintf(stderr, "Syntax: %s [-v] [-L] STORAGE-FILE NEW-STORAGE-FILE "
			"NEW-BASE-ID NEW-MAX-ID NEW-VALUE-SIZE NEW-PROPERTY-SIZE "
			"NEW-QUEUE-CAPACITY\n",
			error_get_program_name());

	exit(-SYNTAX_ERROR);
}

static void show_version(void)
{
	printf("grower %s\n", version_get_source());
	exit(0);
}

static long parse(const char *text, attr_func fn)
{
	long n;
	if (*text == '=')
		return fn(old_store);

	if (FAILED(a2i(text, "%ld", &n)))
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
			show_version();
		default:
			show_syntax();
		}

	if ((argc - optind) != 7)
		show_syntax();

	if (FAILED(storage_open(&old_store, argv[optind++], O_RDONLY)))
		error_report_fatal();

	new_file = argv[optind++];
	new_base_id = parse(argv[optind++], storage_get_base_id);
	new_max_id = parse(argv[optind++], storage_get_max_id);
	new_val_size = parse(argv[optind++], (attr_func)storage_get_value_size);
	new_prop_size = parse(argv[optind++], (attr_func)storage_get_property_size);
	new_q_capacity = parse(argv[optind++], (attr_func)storage_get_queue_capacity);

	if (FAILED(storage_grow(old_store, &new_store, new_file, O_CREAT,
							new_base_id, new_max_id, new_val_size,
							new_prop_size, new_q_capacity)) ||
		FAILED(storage_destroy(&old_store)) ||
		FAILED(storage_destroy(&new_store)))
		error_report_fatal();

	return 0;
}
