/*
  Copyright (c)2014-2017 Peak6 Investments, LP.
  Use of this source code is governed by the COPYING file.
*/

/* signal handling */

#ifndef SIGNALS_H
#define SIGNALS_H

#include <lancaster/status.h>

#ifdef __cplusplus
extern "C" {
#endif

status signal_add_handler(int sig);
status signal_remove_handler(int sig);

status signal_is_raised(int sig);
status signal_any_raised(void);
status signal_clear(int sig);

status signal_on_eintr(const char *func);

#ifdef __cplusplus
}
#endif

#endif
