/* convert text to an integer */

#ifndef A2I_H
#define A2I_H

#include "status.h"

#ifdef __cplusplus
extern "C" {
#endif

status a2i(const char *text, const char *format, void *pnum);

#ifdef __cplusplus
}
#endif

#endif
