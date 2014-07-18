/* signal handling */

#ifndef SIGNALS_H
#define SIGNALS_H

#include "status.h"

#ifdef __cplusplus
extern "C" {
#endif

status signal_add_handler(int sig);
status signal_remove_handler(int sig);

status signal_is_raised(int sig);
status signal_clear(int sig);

#ifdef __cplusplus
}
#endif

#endif
