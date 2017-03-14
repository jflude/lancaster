/*
   Copyright (c)2014-2017 Peak6 Investments, LP.  All rights reserved.
   Use of this source code is governed by the LICENSE file.
*/

/* report statistics etc. by UDP */

#ifndef REPORTER_H
#define REPORTER_H

#include "status.h"

#ifdef __cplusplus
extern "C" {
#endif

struct reporter;
typedef struct reporter *reporter_handle;

status reporter_create(reporter_handle * prep, const char *udp_address,
		       unsigned short udp_port);
status reporter_destroy(reporter_handle * prep);

status reporter_send(reporter_handle rep, const char *msg);

#ifdef __cplusplus
}
#endif

#endif
