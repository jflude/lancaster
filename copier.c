/*
   Copyright (C)2014-2016 Peak6 Investments, LP.  All rights reserved.
   Use of this source code is governed by the LICENSE file.
*/

/* copy contents of a record from one storage to another */

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "a2i.h"
#include "error.h"
#include "storage.h"
#include "version.h"
#include "xalloc.h"

struct arg {
    identifier id;
    record_handle rec;
};

static void show_syntax(void)
{
    fprintf(stderr, "Syntax: %s [-v] [-L] [-V] SOURCE-STORAGE-FILE "
	    "DESTINATION-STORAGE-FILE SOURCE-RECORD-ID "
	    "[SOURCE-RECORD-ID...]\n", error_get_program_name());

    exit(-SYNTAX_ERROR);
}

static void show_version(void)
{
    printf("copier %s\n", version_get_source());
    exit(0);
}

int main(int argc, char *argv[])
{
    storage_handle src_store, dest_store;
    const char *src_file, *dest_file;
    boolean verbose = FALSE;
    struct arg *args, *parg, *last_arg;
    record_handle dest_rec = NULL;
    size_t nrec;
    int opt;

    error_set_program_name(argv[0]);

    while ((opt = getopt(argc, argv, "LVv")) != -1)
	switch (opt) {
	case 'L':
	    error_with_timestamp(TRUE);
	    break;
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

    src_file = argv[optind++];
    dest_file = argv[optind++];

    if (FAILED(storage_open(&src_store, src_file, O_RDWR)) ||
	FAILED(storage_open(&dest_store, dest_file, O_RDWR)))
	error_report_fatal();

    nrec = argc - optind;
    args = xmalloc(nrec * sizeof(struct arg));
    if (!args)
	error_report_fatal();

    last_arg = args + nrec;

    for (parg = args; parg < last_arg; ++parg)
	if (FAILED(a2i(argv[optind++], "%ld", &parg->id)) ||
	    FAILED(storage_get_record(src_store, parg->id, &parg->rec)))
	    error_report_fatal();

    for (parg = args; parg < last_arg; ++parg) {
	status st;
	revision src_rev, dest_rev;
	if (verbose)
	    printf("copying #%08ld [%s]", parg->id, src_file);

	if (FAILED(st = storage_find_next_unused(dest_store, dest_rec,
						 &dest_rec, &dest_rev))) {
	    putchar('\n');
	    error_report_fatal();
	}

	if (!st) {
	    error_msg(STORAGE_FULL, "error: storage is full");
	    putchar('\n');
	    error_report_fatal();
	}

	if (FAILED(record_write_lock(parg->rec, &src_rev))) {
	    record_set_revision(dest_rec, dest_rev);
	    putchar('\n');
	    error_report_fatal();
	}

	st = storage_copy_record(src_store, parg->rec, dest_store, dest_rec,
				 record_get_timestamp(parg->rec), TRUE);

	record_set_revision(dest_rec, src_rev);
	record_set_revision(parg->rec, src_rev);

	if (FAILED(st)) {
	    putchar('\n');
	    error_report_fatal();
	}

	if (verbose) {
	    identifier id;
	    if (FAILED(storage_get_id(dest_store, dest_rec, &id))) {
		putchar('\n');
		error_report_fatal();
	    }

	    printf(" to #%08ld [%s]\n", id, dest_file);
	}
    }

    if (FAILED(storage_destroy(&src_store)) ||
	FAILED(storage_destroy(&dest_store)))
	error_report_fatal();

    return 0;
}
