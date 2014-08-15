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

struct sequence_range_t { sequence low; sequence high; };

#ifdef __cplusplus
}
#endif

#endif
