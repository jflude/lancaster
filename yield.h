/* portably sleep and yield the CPU to another process */

#ifndef YIELD_H
#define YIELD_H

#define CPU_RELAX(x) __asm__ __volatile__("pause" ::: "memory")

#ifdef __cplusplus
extern "C" {
#endif

void yield(void);
void snooze(void);
void slumber(unsigned seconds);

#ifdef __cplusplus
}
#endif

#endif
