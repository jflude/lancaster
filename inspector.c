/* show the attributes and records of a storage */

#include "clock.h"
#include "dump.h"
#include "error.h"
#include "storage.h"
#include "version.h"
#include "xalloc.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define SHOW_ATTRIBUTES 1
#define SHOW_QUEUE 2
#define SHOW_VALUES 4
#define SHOW_PROPERTIES 8

#define SHOW_DIV1 (SHOW_ATTRIBUTES | SHOW_QUEUE)
#define SHOW_DIV2 (SHOW_VALUES | SHOW_PROPERTIES)

static revision rev_copy;
static microsec ts_copy;
static void *val_copy;
static const void *val_base;
static void *prop_copy;
static const void *prop_base;

static void show_syntax(void)
{
	fprintf(stderr, "Syntax: %s [-v] [-a] [-p] [-q] [-V] "
			"STORAGE-FILE [RECORD ID...|all]\n",
			error_get_program_name());

	exit(-SYNTAX_ERROR);
}

static void show_version(void)
{
	printf("inspector %s\n", version_get_source());
	exit(0);
}

static status print_attributes(storage_handle store)
{
	status st;
	microsec when;
	char created[64], touched[64];
	const char *seg_base = (const char *)storage_get_segment(store);
	unsigned short file_ver = storage_get_file_version(store),
		data_ver = storage_get_data_version(store);

	if (FAILED(st = storage_get_created_time(store, &when)) ||
		FAILED(st = clock_get_text(when, 6, created, sizeof(created))) ||
		FAILED(st = storage_get_touched_time(store, &when)) ||
		FAILED(st = clock_get_text(when, 6, touched, sizeof(touched))))
		return st;

	if (printf("storage:          %s\n"
			   "description:      \"%s\"\n"
			   "file version:     %d.%d\n"
			   "data version:     %d.%d\n"
			   "base id:          %ld\n"
			   "max id:           %ld\n"
			   "record size:      %lu\n"
			   "value size:       %lu\n"
			   "property size:    %lu\n"
			   "value offset:     %lu\n"
			   "property offset:  %lu\n"
			   "timestamp offset: %lu\n"
			   "queue base ref:   0x%012lX\n"
			   "queue capacity:   %lu\n"
			   "queue head ref:   0x%012lX\n"
			   "queue head:       %lu\n"
			   "array base ref:   0x%012lx\n"
			   "created time:     %s\n"
			   "touched time:     %s\n",
			   storage_get_file(store),
			   storage_get_description(store),
			   (int)file_ver >> 8,
			   (int)file_ver & 0xFF,
			   (int)data_ver >> 8,
			   (int)data_ver & 0xFF,
			   storage_get_base_id(store),
			   storage_get_max_id(store),
			   storage_get_record_size(store),
			   storage_get_value_size(store),
			   storage_get_property_size(store),
			   storage_get_value_offset(store),
			   storage_get_property_offset(store),
			   storage_get_timestamp_offset(store),
			   (const char *)storage_get_queue_base_ref(store) - seg_base,
			   storage_get_queue_capacity(store),
			   (const char *)storage_get_queue_head_ref(store) - seg_base,
			   storage_get_queue_head(store),
			   (const char *)storage_get_array(store) - seg_base,
			   created,
			   touched) < 0)
		return (feof(stdin) ? error_eof : error_errno)("printf");

	return OK;
}

static status print_div1(void)
{
	if (puts("--------------------------------------------------") < 0)
		return (feof(stdin) ? error_eof : error_errno)("puts");

	return OK;
}

static status print_queue(storage_handle store)
{
	status st;
	q_index i;
	identifier id;
	size_t cap = storage_get_queue_capacity(store);
	q_index q_head = storage_get_queue_head(store) & (cap - 1);
	static char head[] = " <--";

	for (i = 0; (size_t)i < cap; ++i) {
		if (FAILED(st = storage_read_queue(store, i, &id)))
			return st;

		if (printf("%08ld #%08ld%s\n", i, id, (q_head == i ? head : "")) < 0)
			return (feof(stdin) ? error_eof : error_errno)("printf");
	}

	return OK;
}

static status copy_record(storage_handle store, record_handle rec)
{
	status st;
	size_t val_sz = storage_get_value_size(store);
	size_t prop_sz = storage_get_property_size(store);
	size_t offset = (char *)rec - (char *)storage_get_segment(store);

	if (!val_copy) {
		val_copy = xmalloc(val_sz);
		if (!val_copy)
			return NO_MEMORY;
	}

	val_base = (char *)val_copy - offset - storage_get_value_offset(store);

	if (prop_sz > 0 && !prop_copy) {
		prop_copy = xmalloc(prop_sz);
		if (!prop_copy)
			return NO_MEMORY;
	}

	prop_base = (char *)prop_copy - offset - storage_get_property_offset(store);

	do {
		if (FAILED(st = record_read_lock(rec, &rev_copy)))
			return st;

		ts_copy = record_get_timestamp(rec);
		memcpy(val_copy, record_get_value_ref(rec), val_sz);
		if (prop_sz > 0)
			memcpy(prop_copy, storage_get_property_ref(store, rec), prop_sz);
	} while (rev_copy != record_get_revision(rec));

	return OK;
}

static status print_record_header(storage_handle store, record_handle rec)
{
	status st;
	identifier id;
	char buf[128], ts_text[64];
	static const char divider[] =
		"======================================="
		"=======================================";

	if (FAILED(st = storage_get_id(store, rec, &id)) ||
		FAILED(st = clock_get_text(ts_copy, 6, ts_text, sizeof(ts_text))))
		return st;

	st = sprintf(buf, " #%08ld [0x%012lX] rev %08ld %s",
				 id, (char *)rec - (char *)storage_get_array(store),
				 rev_copy, ts_text);
	if (st < 0)
		error_errno("sprintf");

	if (printf("%s%s\n", divider + st + 1, buf) < 0)
		return (feof(stdin) ? error_eof : error_errno)("printf");

	return OK;
}

static status print_value(storage_handle store)
{
	status st;
	if (FAILED(st = dump(val_copy, val_base, storage_get_value_size(store))))
		return st;

	return OK;
}

static status print_div2(void)
{
	if (puts("*") == EOF)
		return (feof(stdin) ? error_eof : error_errno)("puts");

	return OK;
}

static status print_property(storage_handle store)
{
	size_t sz = storage_get_property_size(store);
	if (sz > 0) {
		status st;
		if (FAILED(st = dump(prop_copy, prop_base, sz)))
			return st;
	}

	return OK;
}

static status iter_func(storage_handle store, record_handle rec, void *param)
{
	status st;
	int show = (long)param;

	if (FAILED(st = copy_record(store, rec)) ||
		FAILED(st = print_record_header(store, rec)) ||
		((show & SHOW_VALUES) && FAILED(st = print_value(store))) ||
		(((show & SHOW_DIV2) == SHOW_DIV2) && FAILED(print_div2())) ||
		((show & SHOW_PROPERTIES) && FAILED(st = print_property(store))))
		return st;

	return TRUE;
}

int main(int argc, char *argv[])
{
	storage_handle store;
	int show = 0;
	int opt;

	error_set_program_name(argv[0]);

	while ((opt = getopt(argc, argv, "apqVv")) != -1)
		switch (opt) {
		case 'a':
			show |= SHOW_ATTRIBUTES;
			break;
		case 'p':
			show |= SHOW_PROPERTIES;
			break;
		case 'q':
			show |= SHOW_QUEUE;
			break;
		case 'V':
			show |= SHOW_VALUES;
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

	if (((show & SHOW_ATTRIBUTES) && FAILED(print_attributes(store))) ||
		(((show & SHOW_DIV1) == SHOW_DIV1) && FAILED(print_div1())) ||
		((show & SHOW_QUEUE) && FAILED(print_queue(store))))
		error_report_fatal();

	if (argc > optind && strcmp(argv[optind], "all") == 0) {
		if (FAILED(storage_iterate(store, iter_func, NULL,
								   (void *)(long)show)))
			error_report_fatal();
	} else {
		for (; optind < argc; ++optind) {
			record_handle rec = NULL;
			identifier id = atoi(argv[optind]);

			if (FAILED(storage_get_record(store, id, &rec)) ||
				FAILED(iter_func(store, rec, (void *)(long)show)))
				error_report_fatal();
		}
	}

	XFREE(val_copy);
	XFREE(prop_copy);

	if (FAILED(storage_destroy(&store)))
		error_report_fatal();

	return 0;
}
