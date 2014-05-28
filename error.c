#include "error.h"
#include "status.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char error_desc[128], save_desc[128];
static int error_code, save_code;
static boolean save_used;

static void error_capture(const char* func, int code)
{
	error_code = code;
	sprintf(error_desc, "%s: %s\n", func, strerror(code));
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
	error_code = EOF;
	sprintf(error_desc, "%s: end of file\n", func);
}

void error_errno(const char* func)
{
	error_capture(func, errno);
}

void error_heartbeat(const char* func)
{
	error_capture(func, ETIMEDOUT);
}

void error_invalid_arg(const char* func)
{
	error_capture(func, EINVAL);
}

void error_unimplemented(const char* func)
{
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
