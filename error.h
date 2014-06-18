/* customization of the handling of library errors */

#ifndef ERROR_H
#define ERROR_H

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*error_proc)(int error_code, const char* error_desc);

error_proc error_set_proc(error_proc new_proc);

int error_last_code(void);
const char* error_last_desc(void);

void error_eof(const char* func);
void error_errno(const char* func);
void error_heartbeat(const char* func);	
void error_invalid_arg(const char* func);
void error_unimplemented(const char* func);

void error_save_last(void);
void error_restore_last(void);

void error_report_fatal(void);

#ifdef __cplusplus
}
#endif

#endif
