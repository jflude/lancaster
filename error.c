#include "error.h"
#include "spin.h"
#include "status.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static spin_lock_t capture_lock;
static error_func custom_fn;
static char last_desc[128], saved_desc[128];
static int last_code, saved_code;
static boolean is_saved;

static void capture(const char* func, int code)
{
	SPIN_LOCK(&capture_lock);

	last_code = code;
	sprintf(last_desc, "%s: %s\n", func, strerror(last_code));

	if (custom_fn)
		custom_fn(last_code, last_desc);

	SPIN_UNLOCK(&capture_lock);
}

error_func error_set_func(error_func new_fn)
{
	error_func old_fn;
	SPIN_LOCK(&capture_lock);

	old_fn = custom_fn;
	custom_fn = new_fn;

	SPIN_UNLOCK(&capture_lock);
	return old_fn;
}

int error_last_code(void)
{
	return last_code;
}

const char* error_last_desc(void)
{
	return last_desc;
}

void error_eof(const char* func)
{
	if (!func) {
		error_invalid_arg("error_eof");
		error_report_fatal();
		return;
	}

	SPIN_LOCK(&capture_lock);

	last_code = EOF;
	sprintf(last_desc, "%s: end of file\n", func);

	if (custom_fn)
		custom_fn(last_code, last_desc);

	SPIN_UNLOCK(&capture_lock);
}

void error_errno(const char* func)
{
	if (!func) {
		error_invalid_arg("error_errno");
		error_report_fatal();
		return;
	}

	capture(func, errno);
}

void error_heartbeat(const char* func)
{
	if (!func) {
		error_invalid_arg("error_heartbeat");
		error_report_fatal();
		return;
	}

	capture(func, ETIMEDOUT);
}

void error_invalid_arg(const char* func)
{
	if (!func) {
		error_invalid_arg("error_invalid_arg");
		error_report_fatal();
		return;
	}

	capture(func, EINVAL);
}

void error_unimplemented(const char* func)
{
	if (!func) {
		error_invalid_arg("error_unimplemented");
		error_report_fatal();
		return;
	}

	capture(func, ENOSYS);
}

void error_save_last(void)
{
	SPIN_LOCK(&capture_lock);

	if (!is_saved) {
		saved_code = last_code;
		strcpy(saved_desc, last_desc);
		is_saved = TRUE;
	}

	SPIN_UNLOCK(&capture_lock);
}

void error_restore_last(void)
{
	SPIN_LOCK(&capture_lock);

	if (is_saved) {
		last_code = saved_code;
		strcpy(last_desc, saved_desc);
		is_saved = FALSE;
	}

	SPIN_UNLOCK(&capture_lock);
}

void error_report_fatal(void)
{
	fputs(error_last_desc(), stderr);
	exit(EXIT_FAILURE);
}
