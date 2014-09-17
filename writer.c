/* test writer */

#include "clock.h"
#include "datum.h"
#include "error.h"
#include "signals.h"
#include "storage.h"
#include "thread.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* #define SCATTER_UPDATES */

#ifdef SCATTER_UPDATES
#include "twist.h"
#include <time.h>
#endif

static storage_handle store;
static microsec delay;

static void show_syntax(void)
{
	fprintf(stderr, "Syntax: %s [-v] [-p ERROR PREFIX] STORAGE-FILE "
			"CHANGE-QUEUE-SIZE DELAY\n", error_get_program_name());

	exit(SYNTAX_ERROR);
}

static void show_version(void)
{
	printf("writer 1.0\n");
	exit(0);
}

static status update(identifier id, long n)
{
	record_handle rec = NULL;
	struct datum* d;
	version ver;
	microsec now;
	status st = OK;

	if (FAILED(st = signal_is_raised(SIGHUP)) ||
		FAILED(st = signal_is_raised(SIGINT)) ||
		FAILED(st = signal_is_raised(SIGTERM)) ||
		FAILED(st = storage_get_record(store, id, &rec)) ||
		FAILED(st = clock_time(&now)))
		return st;

	d = record_get_value_ref(rec);
	ver = record_write_lock(rec);

	d->xyz = n;
	d->ts = now;

	record_set_version(rec, NEXT_VER(ver));

	if (FAILED(st = storage_write_queue(store, id)) ||
		(delay > 0 && FAILED(st = clock_sleep(delay))))
		return st;

	return OK;
}

static void* touch_func(thread_handle thr)
{
	status st = OK;
	while (!thread_is_stopping(thr)) {
		microsec now;
		if (FAILED(st = clock_time(&now)) ||
			FAILED(st = storage_touch(store, now)) ||
			FAILED(st = clock_sleep(1000000)))
			break;
	}

	return (void*) (long) st;
}

int main(int argc, char* argv[])
{
	status st = OK;
	thread_handle touch_thread;
	const char* mmap_file;
	size_t q_capacity;
	long xyz = 0;
	int opt;

#ifdef SCATTER_UPDATES
	twist_handle twister;
#endif

	char prog_name[256];
	strcpy(prog_name, argv[0]);
	error_set_program_name(prog_name);

	while ((opt = getopt(argc, argv, "p:v")) != -1)
		switch (opt) {
		case 'p':
			strcat(prog_name, ": ");
			strcat(prog_name, optarg);
			error_set_program_name(prog_name);
			break;
		case 'v':
			show_version();
		default:
			show_syntax();
		}

	if ((argc - optind) != 3)
		show_syntax();

	mmap_file = argv[optind++];
	q_capacity = atoi(argv[optind++]);
	delay = atoi(argv[optind++]);

	if (FAILED(signal_add_handler(SIGHUP)) ||
		FAILED(signal_add_handler(SIGINT)) ||
		FAILED(signal_add_handler(SIGTERM)) ||
		FAILED(storage_create(&store, mmap_file, FALSE, O_CREAT, 0, MAX_ID,
							  sizeof(struct datum), 0, q_capacity)) ||
		FAILED(storage_set_persistence(store, FALSE)) ||
		FAILED(storage_set_description(store, "TEST")) ||
		FAILED(storage_reset(store)) ||
		FAILED(thread_create(&touch_thread, touch_func, NULL)))
		error_report_fatal();

#ifdef SCATTER_UPDATES
	if (FAILED(twist_create(&twister)))
		error_report_fatal();

	twist_seed(twister, (unsigned) time(NULL));
#endif

	for (;;) {
		identifier id;
#ifdef SCATTER_UPDATES
		id = twist_rand(twister) % MAX_ID;
#else
		for (id = 0; id < MAX_ID; ++id)
#endif
			if (FAILED(st = update(id, xyz++)))
				goto finish;
	}

finish:
	if (FAILED(st) ||
		FAILED(thread_destroy(&touch_thread)) ||
#ifdef SCATTER_UPDATES
		FAILED(twist_destroy(&twister)) ||
#endif
		FAILED(storage_destroy(&store)) ||
		FAILED(signal_remove_handler(SIGHUP)) ||
		FAILED(signal_remove_handler(SIGINT)) ||
		FAILED(signal_remove_handler(SIGTERM)))
		error_report_fatal();

	return 0;
}
