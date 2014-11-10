/* copy contents of a record from one storage to another */

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "a2i.h"
#include "clock.h"
#include "error.h"
#include "storage.h"
#include "version.h"

static void show_syntax(void)
{
	fprintf(stderr, "Syntax: %s [-v] [-V] SOURCE-STORAGE-FILE "
			"DESTINATION-STORAGE-FILE SOURCE-RECORD-ID "
			"[SOURCE-RECORD-ID ...]\n",
			error_get_program_name());

	exit(-SYNTAX_ERROR);
}

static void show_version(void)
{
	printf("transfer %s\n", version_get_source());
	exit(0);
}

int main(int argc, char *argv[])
{
	storage_handle src_store, dest_store;
	boolean verbose = FALSE;
	int opt;

	error_set_program_name(argv[0]);

	while ((opt = getopt(argc, argv, "Vv")) != -1)
		switch (opt) {
		case 'V':
			verbose = TRUE;
			break;
		case 'v':
			show_version();
		default:
			show_syntax();
		}

	if ((argc - optind) < 3)
		show_syntax();

	if (FAILED(storage_open(&src_store, argv[optind++], O_RDWR)) ||
		FAILED(storage_open(&dest_store, argv[optind++], O_RDWR)))
		error_report_fatal();

	for (; optind < argc; ++optind) {
		record_handle src_rec, dest_rec;
		revision src_rev, dest_rev;
		identifier id;
		status st = OK;

		if (FAILED(a2i(argv[optind], "%ld", &id)))
			error_report_fatal();

		if (verbose)
			printf("%ld", id);

		if (FAILED(storage_get_record(src_store, id, &src_rec)) ||
			FAILED(st = storage_find_first_unused(dest_store, &dest_rec, &dest_rev))) {
			putchar('\n');
			error_report_fatal();
		}

		if (!st) {
			putchar('\n');
			error_msg("error: storage is full", STORAGE_FULL);
			error_report_fatal();
		}

		if (FAILED(record_write_lock(src_rec, &src_rev))) {
			record_set_revision(dest_rec, dest_rev);
			putchar('\n');
			error_report_fatal();
		}

		st = storage_copy_record(src_store, src_rec, dest_store, dest_rec,
								 record_get_timestamp(src_rec), TRUE);

		record_set_revision(dest_rec, src_rev);
		record_set_revision(src_rec, src_rev);

		if (FAILED(st)) {
			putchar('\n');
			error_report_fatal();
		} else {
			if (FAILED(storage_get_id(dest_store, dest_rec, &id))) {
				putchar('\n');
				error_report_fatal();
			}

			printf(" --> %ld\n", id);
		}
	}

	if (FAILED(storage_destroy(&src_store)) ||
		FAILED(storage_destroy(&dest_store)))
		error_report_fatal();

	return 0;
}
