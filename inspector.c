/* show the attributes and records of a storage */

#include "clock.h"
#include "dump.h"
#include "error.h"
#include "storage.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void show_syntax(void)
{
	fprintf(stderr, "Syntax: %s [-v] STORAGE-FILE\n", error_get_program_name());
	exit(SYNTAX_ERROR);
}

static void show_version(void)
{
	printf("inspector 1.0\n");
	exit(0);
}

static status iter_func(record_handle rec, void* param)
{
	(void) rec; (void) param;
	return TRUE;
}

int main(int argc, char* argv[])
{
	storage_handle store;
	char created_time[64], touched_time[64];

	error_set_program_name(argv[0]);

	if (argc != 2)
		show_syntax();

	if (strcmp(argv[1], "-v") == 0)
		show_version();

	if (FAILED(storage_open(&store, argv[1], O_RDONLY)) ||
		FAILED(clock_get_text(storage_get_created_time(store),
							  created_time, sizeof(created_time))) ||
		FAILED(clock_get_text(storage_get_touched_time(store),
							  touched_time, sizeof(touched_time))))
		error_report_fatal();

	printf("file:           \"%s\"\n"
		   "description:    \"%s\"\n"
		   "lib version:    0x%hX\n"
		   "app version:    0x%hX\n"
		   "base id:        %ld\n"
		   "max id:         %ld\n"
		   "record size:    %lu\n"
		   "value size:     %lu\n"
		   "property size:  %lu\n"
		   "queue capacity: %lu\n"
		   "created time:   %s\n"
		   "touched time:   %s\n",
		   storage_get_file(store),
		   storage_get_description(store),
		   storage_get_lib_version(store),
		   storage_get_app_version(store),
		   storage_get_base_id(store),
		   storage_get_max_id(store),
		   storage_get_record_size(store),
		   storage_get_value_size(store),
		   storage_get_property_size(store),
		   storage_get_queue_capacity(store),
		   created_time,
		   touched_time);

	if (FAILED(storage_iterate(store, iter_func, NULL, NULL)) ||
		FAILED(storage_destroy(&store)))
		error_report_fatal();

	return 0;
}
