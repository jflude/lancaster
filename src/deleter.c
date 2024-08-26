/*
  Copyright (c)2014-2017 Peak6 Investments, LP.
  Use of this source code is governed by the COPYING file.
*/

/* delete a storage */

#include <lancaster/error.h>
#include <lancaster/storage.h>
#include <lancaster/version.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static void show_syntax(void)
{
    fprintf(stderr, "Syntax: %s [-v] [-f] [-L] STORAGE-FILE "
	    "[STORAGE-FILE...]\n", error_get_program_name());

    exit(-SYNTAX_ERROR);
}

int main(int argc, char *argv[])
{
    boolean force = FALSE;
    int opt;

    error_set_program_name(argv[0]);

    while ((opt = getopt(argc, argv, "fLv")) != -1)
	switch (opt) {
	case 'f':
	    force = TRUE;
	    break;
	case 'L':
	    error_with_timestamp(TRUE);
	    break;
	case 'v':
	    show_version("deleter");
	    /* fall through */
	default:
	    show_syntax();
	}

    if ((argc - optind) < 1)
	show_syntax();

    for (; optind < argc; ++optind)
	if (FAILED(storage_delete(argv[optind], force))) {
	    error_append_msg(" [");
	    error_append_msg(argv[optind]);
	    error_append_msg("]");
	    error_report_fatal();
	}

    return 0;
}
