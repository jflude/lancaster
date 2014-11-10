/* erase a record from a storage */

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "a2i.h"
#include "error.h"
#include "storage.h"
#include "version.h"

static void show_syntax(void)
{
	fprintf(stderr, "Syntax: %s [-v] [-H] [-V] STORAGE-FILE "
			"RECORD-ID [RECORD-ID ...]\n",
			error_get_program_name());

	exit(-SYNTAX_ERROR);
}

static void show_version(void)
{
	printf("eraser %s\n", version_get_source());
	exit(0);
}

int main(int argc, char *argv[])
{
	storage_handle store;
	boolean leave_hole = FALSE, verbose = FALSE;
	int opt;

	error_set_program_name(argv[0]);

	while ((opt = getopt(argc, argv, "HVv")) != -1)
		switch (opt) {
		case 'H':
			leave_hole = TRUE;
			break;
		case 'V':
			verbose = TRUE;
			break;
		case 'v':
			show_version();
		default:
			show_syntax();
		}

	if ((argc - optind) < 2)
		show_syntax();

	if (FAILED(storage_open(&store, argv[optind++], O_RDWR)))
		error_report_fatal();

	for (; optind < argc; ++optind) {
		identifier id;
		record_handle rec;
		revision rev;

		if (FAILED(a2i(argv[optind], "%ld", &id)))
			error_report_fatal();

		if (verbose)
			printf("%ld\n", id);

		if (FAILED(storage_get_record(store, id, &rec)))
			error_report_fatal();

		if (leave_hole) {
			if (FAILED(record_write_lock(rec, &rev)))
				error_report_fatal();

			if (FAILED(storage_clear_record(store, rec))) {
				record_set_revision(rec, rev);
				error_report_fatal();
			}
		} else {
			record_handle last_rec;
			revision last_rev;
			if (FAILED(storage_find_last_used(store, &last_rec, &last_rev)))
				error_report_fatal();

			if (last_rec == rec) {
				if (FAILED(storage_clear_record(store, rec)))
					error_report_fatal();
			} else {
				if (FAILED(record_write_lock(rec, &rev))) {
					record_set_revision(last_rec, last_rev);
					error_report_fatal();
				}

				if (FAILED(storage_copy_record(store, last_rec, store, rec,
											   record_get_timestamp(last_rec),
											   TRUE))) {
					record_set_revision(last_rec, last_rev);
					record_set_revision(rec, rev);
					error_report_fatal();
				}

				if (FAILED(storage_clear_record(store, last_rec))) {
					record_set_revision(last_rec, last_rev);
					error_report_fatal();
				}

				record_set_revision(rec, last_rev);
			}
		}
	}

	if (FAILED(storage_destroy(&store)))
		error_report_fatal();

	return 0;
}
