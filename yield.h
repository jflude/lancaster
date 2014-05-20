/* portably sleep and yield the CPU to another process */

#ifndef YIELD_H
#define YIELD_H

#ifdef __cplusplus
extern "C" {
#endif

void yield(void);
void snooze(unsigned seconds);

#ifdef __cplusplus
}
#endif

#endif
