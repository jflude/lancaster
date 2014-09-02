#include "error.h"
#include "spin.h"
#include "status.h"
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static volatile int msg_lock;
static char last_msg[256], saved_msg[256];
static int last_code, saved_code;
static boolean is_saved;

int error_last_code(void)
{
	return last_code;
}

const char* error_last_msg(void)
{
	return last_msg;
}

void error_reset(void)
{
	last_code = 0;
	last_msg[0] = '\0';
}

void error_report_fatal(void)
{
	if (fputs(last_msg, stderr) == EOF || fputc('\n', stderr) == EOF)
		abort();

	exit(EXIT_FAILURE);
}

void error_msg(const char* msg, int code, ...)
{
	va_list ap;
	if (!msg) {
		error_invalid_arg("error_msg");
		error_report_fatal();
		return;
	}

	va_start(ap, code);

	SPIN_WRITE_LOCK(&msg_lock, no_ver);
	last_code = code;
	vsprintf(last_msg, msg, ap);
	SPIN_UNLOCK(&msg_lock, no_ver);

	va_end(ap);
}

static void format(const char* func, const char* desc, int code)
{
	char buf[256];
	sprintf(buf, "%s: %s", func, (desc ? desc : strerror(code)));
	error_msg(buf, code);
}

void error_eof(const char* func)
{
	if (!func) {
		error_invalid_arg("error_eof");
		error_report_fatal();
		return;
	}

	format(func, "end of file", EOF);
}

void error_errno(const char* func)
{
	if (!func) {
		error_invalid_arg("error_errno");
		error_report_fatal();
		return;
	}

	format(func, NULL, errno);
}

void error_invalid_arg(const char* func)
{
	if (!func) {
		error_invalid_arg("error_invalid_arg");
		error_report_fatal();
		return;
	}

	format(func, NULL, EINVAL);
}

void error_unimplemented(const char* func)
{
	if (!func) {
		error_invalid_arg("error_unimplemented");
		error_report_fatal();
		return;
	}

	format(func, NULL, ENOSYS);
}

void error_save_last(void)
{
	SPIN_WRITE_LOCK(&msg_lock, no_ver);

	if (!is_saved) {
		saved_code = last_code;
		strcpy(saved_msg, last_msg);
		is_saved = TRUE;
	}

	SPIN_UNLOCK(&msg_lock, no_ver);
}

void error_restore_last(void)
{
	SPIN_WRITE_LOCK(&msg_lock, no_ver);

	if (is_saved) {
		last_code = saved_code;
		strcpy(last_msg, saved_msg);
		is_saved = FALSE;
	}

	SPIN_UNLOCK(&msg_lock, no_ver);
}
