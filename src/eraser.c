/*
   Copyright (c)2014-2017 Peak6 Investments, LP.  All rights reserved.
   Use of this source code is governed by the LICENSE file.
*/

/* erase a record from a storage */

#include <lancaster/a2i.h>
#include <lancaster/error.h>
#include <lancaster/storage.h>
#include <lancaster/version.h>
#include <lancaster/xalloc.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

struct arg {
    identifier id;
    record_handle rec;
};

static void show_syntax(void)
{
    fprintf(stderr, "Syntax: %s [-v] [-c] [-L] [-V] STORAGE-FILE "
	    "RECORD-ID [RECORD-ID...]\n", error_get_program_name());

    exit(-SYNTAX_ERROR);
}

int main(int argc, char *argv[])
{
    storage_handle store;
    const char *stg_file;
    boolean compact = FALSE, verbose = FALSE;
    struct arg *args, *parg, *last_arg;
    size_t nrec;
    int opt;

    error_set_program_name(argv[0]);

    while ((opt = getopt(argc, argv, "cLVv")) != -1)
	switch (opt) {
	case 'c':
	    compact = TRUE;
	    break;
	case 'L':
	    error_with_timestamp(TRUE);
	    break;
	case 'V':
	    verbose = TRUE;
	    break;
	case 'v':
	    show_version("eraser");
	default:
	    show_syntax();
	}

    if ((argc - optind) < 2)
	show_syntax();

    stg_file = argv[optind++];

    if (FAILED(storage_open(&store, stg_file, O_RDWR)))
	error_report_fatal();

    nrec = argc - optind;
    args = xmalloc(nrec * sizeof(struct arg));
    if (!args)
	error_report_fatal();

    last_arg = args + nrec;

    for (parg = args; parg < last_arg; ++parg)
	if (FAILED(a2i(argv[optind++], "%ld", &parg->id)) ||
	    FAILED(storage_get_record(store, parg->id, &parg->rec)))
	    error_report_fatal();

    for (parg = args; parg < last_arg; ++parg) {
	revision rev;
	if (verbose)
	    printf("erasing #%08ld [%s]\n", parg->id, stg_file);

	if (FAILED(record_write_lock(parg->rec, &rev)))
	    error_report_fatal();

	if (FAILED(storage_clear_record(store, parg->rec))) {
	    record_set_revision(parg->rec, rev);
	    error_report_fatal();
	}
    }

    if (compact) {
	revision rev, last_rev;
	record_handle last_rec = NULL;

	for (parg = args; parg < last_arg; ++parg) {
	    status st;
	    if (FAILED(st = storage_find_prev_used(store, last_rec,
						   &last_rec, &last_rev)))
		error_report_fatal();

	    if (!st)
		break;

	    if (parg->rec >= last_rec)
		record_set_revision(last_rec, last_rev);
	    else {
		if (verbose)
		    printf("compacting #%08ld [%s]\n", parg->id, stg_file);

		if (FAILED(record_write_lock(parg->rec, &rev))) {
		    record_set_revision(last_rec, last_rev);
		    error_report_fatal();
		}

		if (FAILED(storage_copy_record(store, last_rec,
					       store, parg->rec,
					       record_get_timestamp(last_rec),
					       TRUE))) {
		    record_set_revision(last_rec, last_rev);
		    record_set_revision(parg->rec, rev);
		    error_report_fatal();
		}

		if (FAILED(storage_clear_record(store, last_rec))) {
		    record_set_revision(last_rec, last_rev);
		    error_report_fatal();
		}

		record_set_revision(parg->rec, last_rev);
	    }
	}
    }

    if (FAILED(storage_destroy(&store)))
	error_report_fatal();

    return 0;
}
