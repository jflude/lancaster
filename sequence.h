/*
   Copyright (C)2014-2016 Peak6 Investments, LP.  All rights reserved.
   Use of this source code is governed by the LICENSE file.
*/

/* packet sequence numbers */

#ifndef SEQUENCE_H
#define SEQUENCE_H

#include <limits.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef long sequence;

#define SEQUENCE_MIN LONG_MIN
#define SEQUENCE_MAX LONG_MAX

#define HEARTBEAT_SEQ -1
#define WILL_QUIT_SEQ -2

struct sequence_range {
    sequence low, high;
};

#define IS_VALID_RANGE(r) ((r).low < (r).high)
#define IS_WITHIN_RANGE(r, n) ((n) >= (r).low && (n) < (r).high)

#define INVALIDATE_RANGE(r) \
	((void)((r).low = SEQUENCE_MAX, (r).high = SEQUENCE_MIN))

#ifdef __cplusplus
}
#endif

#endif
