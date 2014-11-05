/* modify the attributes of a storage */

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "error.h"
#include "storage.h"
#include "version.h"

#define PARSE(n, f) \
	(*argv[n] == '=' ? f(old_store) : (unsigned)atoi(argv[n]))

static void show_syntax(void)
{
	fprintf(stderr, "Syntax: %s [-v] STORAGE-FILE NEW-STORAGE-FILE NEW-BASE-ID "
			"NEW-MAX-ID NEW-VALUE-SIZE NEW-PROPERTY-SIZE NEW-QUEUE-CAPACITY\n",
			error_get_program_name());

	exit(-SYNTAX_ERROR);
}

static void show_version(void)
{
	printf("grower %s\n", version_get_source());
	exit(0);
}

int main(int argc, char *argv[])
{
	storage_handle old_store, new_store;
	identifier new_base_id, new_max_id;
	size_t new_val_size, new_prop_size, new_q_capacity;
	const char *new_file;
	int opt;

	error_set_program_name(argv[0]);

	while ((opt = getopt(argc, argv, "v")) != -1)
		switch (opt) {
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
	new_base_id = PARSE(optind, storage_get_base_id); optind++;
	new_max_id = PARSE(optind, storage_get_max_id); optind++;
	new_val_size = PARSE(optind, storage_get_value_size); optind++;
	new_prop_size = PARSE(optind, storage_get_property_size); optind++;
	new_q_capacity = PARSE(optind, storage_get_queue_capacity);

	if (FAILED(storage_grow(old_store, &new_store, new_file, new_base_id,
							new_max_id, new_val_size, new_prop_size,
							new_q_capacity)) ||
		FAILED(storage_destroy(&old_store)) ||
		FAILED(storage_destroy(&new_store)))
		error_report_fatal();

	return 0;
}
