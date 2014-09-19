/* modify the attributes of a storage */

#include "error.h"
#include "storage.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PARSE(n, f) \
	(strcmp(argv[n], "=") == 0 ? f(old_store) : (unsigned) atoi(argv[n]))

static void show_syntax(void)
{
	fprintf(stderr, "Syntax: %s [-v] STORAGE-FILE NEW-STORAGE-FILE NEW-BASE-ID "
			"NEW-MAX-ID NEW-VALUE-SIZE NEW-PROPERTY-SIZE NEW-QUEUE-CAPACITY\n",
			error_get_program_name());

	exit(SYNTAX_ERROR);
}

static void show_version(void)
{
	puts("grower " SOURCE_VERSION);
	exit(0);
}

int main(int argc, char* argv[])
{
	storage_handle old_store, new_store;
	identifier new_base_id, new_max_id;
	size_t new_val_size, new_prop_size, new_q_capacity;

	error_set_program_name(argv[0]);

	if (argc < 2)
		show_syntax();

	if (strcmp(argv[1], "-v") == 0)
		show_version();

	if (argc != 8)
		show_syntax();

	if (FAILED(storage_open(&old_store, argv[1], O_RDONLY)))
		error_report_fatal();

	new_base_id = PARSE(3, storage_get_base_id);
	new_max_id = PARSE(4, storage_get_max_id);
	new_val_size = PARSE(5, storage_get_value_size);
	new_prop_size = PARSE(6, storage_get_property_size);
	new_q_capacity = PARSE(7, storage_get_queue_capacity);

	if (FAILED(storage_grow(old_store, &new_store, argv[2], new_base_id,
							new_max_id, new_val_size, new_prop_size,
							new_q_capacity)) ||
		FAILED(storage_destroy(&old_store)) ||
		FAILED(storage_destroy(&new_store)))
		error_report_fatal();

	return 0;
}
