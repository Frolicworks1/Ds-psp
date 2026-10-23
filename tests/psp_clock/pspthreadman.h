/* Minimal compile-time fixture for the PSP clock adapter; not a device emulator. */
#ifndef DS_TEST_PSPTHREADMAN_H
#define DS_TEST_PSPTHREADMAN_H
#include <stdint.h>
typedef struct { uint32_t low,hi; } SceKernelSysClock;
typedef struct { unsigned size; SceKernelSysClock runClocks; } SceKernelThreadRunStatus;
int sceKernelGetThreadId(void);
int sceKernelReferThreadRunStatus(int id,SceKernelThreadRunStatus *status);
int sceKernelSysClock2USec(SceKernelSysClock *clock,unsigned *seconds,unsigned *microseconds);
#endif
