/* customization of the handling of library errors */

#ifndef ERROR_H
#define ERROR_H

#ifdef __cplusplus
extern "C" {
#endif

int error_last_code(void);
const char* error_last_msg(void);

void error_reset(void);
void error_report_fatal(void);

void error_msg(const char* msg, int code, ...);

void error_eof(const char* func);
void error_errno(const char* func);
void error_invalid_arg(const char* func);
void error_unimplemented(const char* func);

void error_save_last(void);
void error_restore_last(void);

#ifdef __cplusplus
}
#endif

#endif
