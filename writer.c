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
#include "thread.h"
#include "twist.h"
#include "version.h"

#define STORAGE_PERM (S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH)

static storage_handle store;
static microsec delay;

static void show_syntax(void)
{
	fprintf(stderr, "Syntax: %s [-v] [-p ERROR PREFIX] "
			"[-q CHANGE-QUEUE-CAPACITY] [-r] STORAGE-FILE DELAY\n",
			error_get_program_name());

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

	if (FAILED(st = signal_is_raised(SIGHUP)) ||
		FAILED(st = signal_is_raised(SIGINT)) ||
		FAILED(st = signal_is_raised(SIGTERM)) ||
		FAILED(st = storage_get_record(store, id, &rec)) ||
		FAILED(st = clock_time(&now)))
		return st;

	d = record_get_value_ref(rec);

	if (FAILED(st = record_write_lock(rec, &rev)))
		return st;

	d->xyz = n;

	record_set_timestamp(rec, now);
	record_set_revision(rec, NEXT_REV(rev));

	if (FAILED(st = storage_write_queue(store, id)) ||
		(delay > 0 && FAILED(st = clock_sleep(delay))))
		return st;

	return OK;
}

static void *touch_func(thread_handle thr)
{
	status st = OK;
	while (!thread_is_stopping(thr)) {
		microsec now;
		if (FAILED(st = clock_time(&now)) ||
			FAILED(st = storage_touch(store, now)) ||
			FAILED(st = clock_sleep(1000000)))
			break;
	}

	return (void *)(long)st;
}

int main(int argc, char *argv[])
{
	status st = OK;
	thread_handle touch_thread;
	twist_handle twister;
	const char *mmap_file;
	size_t q_capacity = 0;
	boolean at_random = FALSE;
	long xyz = 0;
	int opt;

	char prog_name[256];
	strcpy(prog_name, argv[0]);
	error_set_program_name(prog_name);

	while ((opt = getopt(argc, argv, "p:q:rv")) != -1)
		switch (opt) {
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
		FAILED(thread_create(&touch_thread, touch_func, NULL)))
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
		FAILED(thread_destroy(&touch_thread)) ||
		(at_random && FAILED(twist_destroy(&twister))) ||
		FAILED(storage_destroy(&store)) ||
		FAILED(signal_remove_handler(SIGHUP)) ||
		FAILED(signal_remove_handler(SIGINT)) ||
		FAILED(signal_remove_handler(SIGTERM)))
		error_report_fatal();

	return 0;
}
