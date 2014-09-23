/* show the attributes and records of a storage */

#include "clock.h"
#include "dump.h"
#include "error.h"
#include "storage.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void show_syntax(void)
{
	fprintf(stderr, "Syntax: %s [-v] [-a] [-p] [-q] [-r] "
			"STORAGE-FILE [RECORD ID...]\n",
			error_get_program_name());

	exit(SYNTAX_ERROR);
}

static void show_version(void)
{
	puts("inspector " SOURCE_VERSION);
	exit(0);
}

static status print_attributes(storage_handle store)
{
	status st;
	char created_time[64], touched_time[64];

	if (FAILED(st = clock_get_text(storage_get_created_time(store), 6,
								   created_time, sizeof(created_time))) ||
		FAILED(st = clock_get_text(storage_get_touched_time(store), 6,
								   touched_time, sizeof(touched_time))))
		return st;

	if (printf("file:           \"%s\"\n"
			   "description:    \"%s\"\n"
			   "lib version:    0x%hX\n"
			   "app version:    0x%hX\n"
			   "base id:        %ld\n"
			   "max id:         %ld\n"
			   "record size:    %lu\n"
			   "value size:     %lu\n"
			   "property size:  %lu\n"
			   "queue capacity: %lu\n"
			   "queue head:     %lu\n"
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
			   storage_get_queue_head(store),
			   created_time,
			   touched_time) < 0)
		return (feof(stdin) ? error_eof : error_errno)("printf");

	return OK;
}

static status print_queue(storage_handle store)
{
	status st;
	q_index i;
	struct q_element elem;
	size_t cap = storage_get_queue_capacity(store);
	char buf[64];
	static char head[] = " <--";

	if (puts("------------------------------------------------") < 0)
		return (feof(stdin) ? error_eof : error_errno)("puts");

	for (i = 0; (size_t) i < cap; ++i) {
		if (FAILED(st = storage_read_queue(store, i, &elem, FALSE)) ||
			FAILED(st = clock_get_text(elem.ts, 6, buf, sizeof(buf))))
			return st;

		if (printf("%08ld #%08ld %s%s\n", i, elem.id, buf,
				   (storage_get_queue_head(store) == i ? head : "")) < 0)
			return (feof(stdin) ? error_eof : error_errno)("printf");
	}

	return OK;
}

static status print_record(storage_handle store, record_handle rec,
						   boolean show_prop)
{
	status st;
	identifier id;
	size_t prop_sz;
	char buf[128];
	static const char divider[] =
		"======================================="
		"=======================================";

	if (FAILED(st = storage_get_id(store, rec, &id)))
		return st;

	st = sprintf(buf, " #%08ld [0x%012lX] rev %08ld ",
				 id, (char*) rec - (char*) storage_get_array(store),
				 record_get_revision(rec));
	if (st < 0)
		error_errno("sprintf");

	if (printf("%s%s\n", divider + st, buf) < 0)
		return (feof(stdin) ? error_eof : error_errno)("printf");

	if (FAILED(st = dump(record_get_value_ref(rec),
						 storage_get_value_size(store), TRUE)))
		return st;

	if (show_prop && (prop_sz = storage_get_property_size(store)) > 0) {
		if (putchar('\n') == EOF)
			return (feof(stdin) ? error_eof : error_errno)("putchar");

		if (FAILED(st = dump(storage_get_property_ref(store, rec),
							 prop_sz, TRUE)))
			return st;
	}

	return OK;
}

static status iter_func(storage_handle store, record_handle rec, void* param)
{
	status st;
	return FAILED(st = print_record(store, rec, (param != NULL))) ? st : TRUE;
}

int main(int argc, char* argv[])
{
	storage_handle store;
	boolean show_attr = FALSE, show_queue = FALSE,
		show_recs = FALSE, show_prop = FALSE;
	int opt;

	error_set_program_name(argv[0]);

	while ((opt = getopt(argc, argv, "apqrv")) != -1)
		switch (opt) {
		case 'a':
			show_attr = TRUE;
			break;
		case 'p':
			show_prop = TRUE;
			break;
		case 'q':
			show_queue = TRUE;
			break;
		case 'r':
			show_recs = TRUE;
			break;
		case 'v':
			show_version();
		default:
			show_syntax();
		}

	if ((argc - optind) < 1)
		show_syntax();

	if (FAILED(storage_open(&store, argv[optind++], O_RDONLY)))
		error_report_fatal();

	if ((show_attr && FAILED(print_attributes(store))) ||
		(show_queue && FAILED(print_queue(store))))
		error_report_fatal();

	if (show_recs) {
		if (FAILED(storage_iterate(store, iter_func, NULL,
								   (void*) (long) show_prop)))
			error_report_fatal();
	} else {
		identifier id;
		record_handle rec = NULL;
		for (; optind < argc; ++optind) {
			id = atoi(argv[optind]);
			if (FAILED(storage_get_record(store, id, &rec)) ||
				FAILED(print_record(store, rec, show_prop)))
				error_report_fatal();
		}
	}

	if (FAILED(storage_destroy(&store)))
		error_report_fatal();

	return 0;
}
