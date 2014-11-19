/* delete a storage */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "error.h"
#include "storage.h"
#include "version.h"

static void show_syntax(void)
{
	fprintf(stderr, "Syntax: %s [-v] STORAGE-FILE\n", error_get_program_name());
	exit(-SYNTAX_ERROR);
}

static void show_version(void)
{
	printf("deleter %s\n", version_get_source());
	exit(0);
}

int main(int argc, char *argv[])
{
	int opt;
	error_set_program_name(argv[0]);

	while ((opt = getopt(argc, argv, "v")) != -1)
		switch (opt) {
		case 'v':
			show_version();
		default:
			show_syntax();
		}

	if ((argc - optind) != 1)
		show_syntax();

	if (FAILED(storage_delete(argv[optind])))
		error_report_fatal();

	return 0;
}