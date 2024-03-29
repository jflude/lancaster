/*
  Copyright (c)2014-2017 Peak6 Investments, LP.
  Copyright (c)2018-2024 Justin Flude.
  Use of this source code is governed by the COPYING file.
*/

/* customization of the handling of library errors */

#ifndef ERROR_H
#define ERROR_H

#include <lancaster/status.h>

#ifdef __cplusplus
extern "C" {
#endif

const char *error_get_program_name(void);
void error_set_program_name(const char *name);

boolean error_with_timestamp(boolean with_timestamp);

int error_last_code(void);
const char *error_last_msg(void);
void error_save_last(void);
void error_restore_last(void);
void error_reset(void);

int error_msg(int code, const char *msg, ...);
int error_eof(const char *func);
int error_errno(const char *func);
int error_eintr(const char *func);
int error_invalid_arg(const char *func);
int error_unimplemented(const char *func);

void error_append_msg(const char *text);
void error_report_fatal(void);

#ifdef __cplusplus
}
#endif

#endif
