#include "error.h"
#include "status.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static error_proc error_custom_proc;
static char error_desc[128], save_desc[128];
static int error_code, save_code;
static boolean save_used;

static void error_capture(const char* func, int code)
{
	error_code = code;
	sprintf(error_desc, "%s: %s\n", func, strerror(code));

	if (error_custom_proc)
		error_custom_proc(code, error_desc);
}

error_proc error_set_proc(error_proc new_proc)
{
	error_proc old_proc = error_custom_proc;
	error_custom_proc = new_proc;
	return old_proc;
}

int error_last_code(void)
{
	return error_code;
}

const char* error_last_desc(void)
{
	return error_desc;
}

void error_eof(const char* func)
{
	if (!func) {
		error_invalid_arg("error_eof");
		error_report_fatal();
		return;
	}

	error_code = EOF;
	sprintf(error_desc, "%s: end of file\n", func);

	if (error_custom_proc)
		error_custom_proc(error_code, error_desc);
}

void error_errno(const char* func)
{
	if (!func) {
		error_invalid_arg("error_errno");
		error_report_fatal();
		return;
	}

	error_capture(func, errno);
}

void error_heartbeat(const char* func)
{
	if (!func) {
		error_invalid_arg("error_heartbeat");
		error_report_fatal();
		return;
	}

	error_capture(func, ETIMEDOUT);
}

void error_invalid_arg(const char* func)
{
	if (!func) {
		error_invalid_arg("error_invalid_arg");
		error_report_fatal();
		return;
	}

	error_capture(func, EINVAL);
}

void error_unimplemented(const char* func)
{
	if (!func) {
		error_invalid_arg("error_unimplemented");
		error_report_fatal();
		return;
	}

	error_capture(func, ENOSYS);
}

void error_save_last(void)
{
	if (!save_used) {
		save_code = error_code;
		strcpy(save_desc, error_desc);
		save_used = TRUE;
	}
}

void error_restore_last(void)
{
	if (save_used) {
		error_code = save_code;
		strcpy(error_desc, save_desc);
		save_used = FALSE;
	}
}

void error_report_fatal(void)
{
	fputs(error_last_desc(), stderr);
	exit(EXIT_FAILURE);
}
