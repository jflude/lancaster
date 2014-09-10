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

/* #define SCATTER_UPDATES */

#ifdef SCATTER_UPDATES
#include "twist.h"
#include <time.h>

static twist_handle twister;
#endif

static storage_handle store;
static int delay;

static void syntax(const char* prog)
{
	fprintf(stderr,
			"Syntax: %s STORAGE-FILE-OR-SEGMENT CHANGE-QUEUE-SIZE DELAY\n",
			prog);

	exit(EXIT_FAILURE);
}

static status update(identifier id, long n)
{
	record_handle rec;
	struct datum* d;
	version ver;
	microsec now;
	status st = OK;

	if (signal_is_raised(SIGHUP) ||
		signal_is_raised(SIGINT) ||
		signal_is_raised(SIGTERM))
		return FALSE;

	if (FAILED(st = storage_get_record(store, id, &rec)) ||
		FAILED(st = clock_time(&now)))
		return st;

	d = record_get_value_ref(rec);
	ver = record_write_lock(rec);

	d->xyz = n;
	d->ts = now;

	record_set_version(rec, ver);

	if (FAILED(st = storage_write_queue(store, id)) ||
		(delay > 0 && FAILED(st = clock_sleep(delay))))
		return st;

	return TRUE;
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
	thread_handle touch_thread;
	status st = OK;
	long xyz = 0;

	error_set_program_name(argv[0]);

	if (argc != 4)
		syntax(argv[0]);

	delay = atoi(argv[3]);

	if (FAILED(signal_add_handler(SIGHUP)) ||
		FAILED(signal_add_handler(SIGINT)) ||
		FAILED(signal_add_handler(SIGTERM)) ||
		FAILED(storage_create(&store, argv[1], FALSE, O_CREAT, 0, MAX_ID,
							  sizeof(struct datum), 0, atoi(argv[2]))) ||
		FAILED(storage_set_description(store, "TEST")) ||
		FAILED(storage_reset(store)) ||
		FAILED(thread_create(&touch_thread, touch_func, NULL)))
		error_report_fatal();

#ifdef SCATTER_UPDATES
	if (FAILED(st = twist_create(&twister)))
		return st;

	twist_seed(twister, (unsigned) time(NULL));
#endif

	for (;;) {
		identifier id;
#ifdef SCATTER_UPDATES
		id = twist_rand(twister) % MAX_ID;
#else
		for (id = 0; id < MAX_ID; ++id)
#endif
			if (FAILED(st = update(id, xyz++)) || !st)
				goto finish;
	}

finish:
	thread_destroy(&touch_thread);
	storage_destroy(&store);

#ifdef SCATTER_UPDATES
	twist_destroy(&twister);
#endif

	if (FAILED(st) ||
		FAILED(signal_remove_handler(SIGHUP)) ||
		FAILED(signal_remove_handler(SIGINT)) ||
		FAILED(signal_remove_handler(SIGTERM)))
		error_report_fatal();

	return 0;
}
