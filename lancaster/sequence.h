/*
  Copyright (c)2014-2017 Peak6 Investments, LP.
  Copyright (c)2018-2024 Justin Flude.
  Use of this source code is governed by the COPYING file.
*/

/* packet sequence numbers */

#ifndef SEQUENCE_H
#define SEQUENCE_H

#include <lancaster/int64.h>
#include <limits.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int64_t sequence;

#define SEQUENCE_MIN INT64_MIN
#define SEQUENCE_MAX INT64_MAX

#define HEARTBEAT_SEQ -1
#define WILL_QUIT_SEQ -2

struct sequence_range {
    sequence low, high;
};

#define IS_VALID_RANGE(r) ((r).low < (r).high)
#define IS_WITHIN_RANGE(r, n) ((n) >= (r).low && (n) < (r).high)

#define INVALIDATE_RANGE(r)					\
    ((void)((r).low = SEQUENCE_MAX, (r).high = SEQUENCE_MIN))

#ifdef __cplusplus
}
#endif

#endif
