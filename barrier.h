/* compiler and memory reordering barriers */

#ifndef BARRIER_H
#define BARRIER_H

#define COMPILER_BARRIER(x) __asm__ __volatile__("" ::: "memory")
#define MEMORY_BARRIER(x) __asm__ __volatile__("mfence" ::: "memory")

#endif
