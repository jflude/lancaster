/* portably sleep and yield the CPU to another process */

#include "status.h"

#ifndef YIELD_H
#define YIELD_H

#define CPU_RELAX(x) __asm__ __volatile__("pause" ::: "memory")

#ifdef __cplusplus
extern "C" {
#endif

status yield(void);

#ifdef __cplusplus
}
#endif

#endif
