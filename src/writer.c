/*
   Copyright (c)2014-2017 Peak6 Investments, LP.  All rights reserved.
   Use of this source code is governed by the LICENSE file.
*/

/* test writer */

#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include "a2i.h"
#include "clock.h"
#include "datum.h"
#include "error.h"
#include "signals.h"
#include "storage.h"
#include "toucher.h"
#include "twist.h"
#include "version.h"

#define DEFAULT_QUEUE_CAPACITY 256
#define DEFAULT_TOUCH_USEC (1 * 1000000)
#define STORAGE_PERM (S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH)

static storage_handle store;
static microsec delay;

static void show_syntax(void)
{
    fprintf(stderr, "Syntax: %s [-v] [-L] [-p ERROR PREFIX] "
	    "[-q CHANGE-QUEUE-CAPACITY] [-r] [-T TOUCH-PERIOD] "
	    "STORAGE-FILE DELAY\n", error_get_program_name());

    exit(-SYNTAX_ERROR);
}

static void show_version(void)
{
    printf("writer %s\n", version_get_source());
    exit(0);
}

static status update(identifier id, long n)
{
    record_handle rec = NULL;
    struct datum *d;
    revision rev;
    microsec now;
    status st = OK;

    if (FAILED(st = signal_any_raised()) ||
	FAILED(st = storage_get_record(store, id, &rec)) ||
	FAILED(st = clock_time(&now)))
	return st;

    d = record_get_value_ref(rec);

    if (FAILED(st = record_write_lock(rec, &rev)))
	return st;

    d->xyz = n;

    record_set_timestamp(rec, now);
    record_set_revision(rec, NEXT_REV(rev));

    if ((storage_get_queue_capacity(store) > 0 &&
	 FAILED(st = storage_write_queue(store, id))) ||
	(delay > 0 && FAILED(st = clock_sleep(delay))))
	return st;

    return OK;
}

int main(int argc, char *argv[])
{
    status st = OK;
    twist_handle twister;
    toucher_handle toucher;
    const char *mmap_file;
    size_t q_capacity = DEFAULT_QUEUE_CAPACITY;
    microsec touch_period = DEFAULT_TOUCH_USEC;
    boolean at_random = FALSE;
    long xyz = 0;
    int opt;

    char prog_name[256];
    strcpy(prog_name, argv[0]);
    error_set_program_name(prog_name);

    while ((opt = getopt(argc, argv, "Lp:q:rT:v")) != -1)
	switch (opt) {
	case 'L':
	    error_with_timestamp(TRUE);
	    break;
	case 'p':
	    strcat(prog_name, ": ");
	    strcat(prog_name, optarg);
	    error_set_program_name(prog_name);
	    break;
	case 'q':
	    if (FAILED(a2i(optarg, "%lu", &q_capacity)))
		error_report_fatal();
	    break;
	case 'r':
	    at_random = TRUE;
	    break;
	case 'T':
	    if (FAILED(a2i(optarg, "%ld", &touch_period)))
		error_report_fatal();
	    break;
	case 'v':
	    show_version();
	default:
	    show_syntax();
	}

    if ((argc - optind) != 2)
	show_syntax();

    mmap_file = argv[optind++];

    if (FAILED(a2i(argv[optind++], "%ld", &delay)) ||
	FAILED(signal_add_handler(SIGHUP)) ||
	FAILED(signal_add_handler(SIGINT)) ||
	FAILED(signal_add_handler(SIGTERM)) ||
	FAILED(storage_create(&store, mmap_file, O_RDWR | O_CREAT, STORAGE_PERM,
			      FALSE, 0, MAX_ID, sizeof(struct datum), 0,
			      q_capacity, "TEST")) ||
	FAILED(storage_reset(store)) ||
	FAILED(toucher_create(&toucher, touch_period)) ||
	FAILED(toucher_add_storage(toucher, store)))
	error_report_fatal();

    if (at_random) {
	if (FAILED(twist_create(&twister)))
	    error_report_fatal();

	twist_seed(twister, (unsigned)time(NULL));

	for (;;)
	    if (FAILED(st = update(twist_rand(twister) % MAX_ID, xyz++)))
		goto finish;
    } else {
	identifier id;
	for (;;)
	    for (id = 0; id < MAX_ID; ++id)
		if (FAILED(st = update(id, xyz++)))
		    goto finish;
    }

  finish:
    if (FAILED(st) ||
	FAILED(toucher_destroy(&toucher)) ||
	(at_random && FAILED(twist_destroy(&twister))) ||
	FAILED(storage_destroy(&store)) ||
	FAILED(signal_remove_handler(SIGHUP)) ||
	FAILED(signal_remove_handler(SIGINT)) ||
	FAILED(signal_remove_handler(SIGTERM)))
	error_report_fatal();

    return 0;
}
