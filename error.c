#include "error.h"
#include "spin.h"
#include "status.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static volatile int capture_lock;
static char last_desc[128], saved_desc[128];
static int last_code, saved_code;
static boolean is_saved;

static void capture(const char* func, const char* msg, int code)
{
	SPIN_WRITE_LOCK(&capture_lock, no_ver);
	last_code = code;
	sprintf(last_desc, "%s: %s\n", func, (msg ? msg : strerror(last_code)));
	SPIN_UNLOCK(&capture_lock, no_ver);
}

int error_last_code(void)
{
	return last_code;
}

const char* error_last_desc(void)
{
	return last_desc;
}

void error_reset(void)
{
	last_code = 0;
	last_desc[0] = '\0';
}

void error_report_fatal(void)
{
	fputs(last_desc, stderr);
	exit(EXIT_FAILURE);
}

void error_eof(const char* func)
{
	if (!func) {
		error_invalid_arg("error_eof");
		error_report_fatal();
		return;
	}

	capture(func, "end of file", EOF);
}

void error_errno(const char* func)
{
	if (!func) {
		error_invalid_arg("error_errno");
		error_report_fatal();
		return;
	}

	capture(func, NULL, errno);
}

void error_heartbeat(const char* func)
{
	if (!func) {
		error_invalid_arg("error_heartbeat");
		error_report_fatal();
		return;
	}

	capture(func, "no heartbeat", HEARTBEAT);
}

void error_invalid_arg(const char* func)
{
	if (!func) {
		error_invalid_arg("error_invalid_arg");
		error_report_fatal();
		return;
	}

	capture(func, NULL, EINVAL);
}

void error_unimplemented(const char* func)
{
	if (!func) {
		error_invalid_arg("error_unimplemented");
		error_report_fatal();
		return;
	}

	capture(func, NULL, ENOSYS);
}

void error_save_last(void)
{
	SPIN_WRITE_LOCK(&capture_lock, no_ver);

	if (!is_saved) {
		saved_code = last_code;
		strcpy(saved_desc, last_desc);
		is_saved = TRUE;
	}

	SPIN_UNLOCK(&capture_lock, no_ver);
}

void error_restore_last(void)
{
	SPIN_WRITE_LOCK(&capture_lock, no_ver);

	if (is_saved) {
		last_code = saved_code;
		strcpy(last_desc, saved_desc);
		is_saved = FALSE;
	}

	SPIN_UNLOCK(&capture_lock, no_ver);
}
