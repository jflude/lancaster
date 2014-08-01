/* portably sleep and yield the CPU to another process */

#ifndef YIELD_H
#define YIELD_H

#include "status.h"

#define CPU_RELAX(x) __asm__ __volatile__("pause" ::: "memory")

#ifdef __cplusplus
extern "C" {
#endif

status yield(void);

#ifdef __cplusplus
}
#endif

#endif
