#ifndef DSPSP_IMPORTS_H
#define DSPSP_IMPORTS_H

#include <stdint.h>
#include "arm_loader.h"
struct DsArmv6Cpu;


#define DS_IMPORT_TRAMP_BASE 0xF0000000u

void ds_imports_init(void);
void ds_imports_bind_image(DsArmImage *image);
void *ds_import_resolve(const char *library, const char *symbol);
/* Returns a guest address of a data object, never a host pointer. Zero fails. */
uint32_t ds_import_resolve_data(const char *symbol);
typedef struct {
    uint32_t function, argument, dso;
} DsGuestDestructor;
/* Remove newest matching registration before guest execution (dso=0: any).
 * Returns 1 for a callback, 0 if empty, -1 for a null output. Does not execute it. */
int ds_import_take_destructor(uint32_t dso, DsGuestDestructor *out);
typedef struct {
    uint32_t mmap_live_bytes, mmap_peak_bytes, mmap_largest_request;
    uint32_t mmap_calls, mmap_failures;
} DsGuestMemoryStats;
DsGuestMemoryStats ds_import_memory_stats(void);
unsigned ds_import_resolve_count(void);
int ds_import_is_trampoline(uint32_t guest_addr);
int ds_import_call(uint32_t guest_addr, struct DsArmv6Cpu *cpu);

/* Allocate bounded guest JavaVM/JNIEnv tables; unsupported slots trap. */
int ds_imports_prepare_jni(uint32_t *vm,uint32_t *env);

/* Assign the single platform Activity before NativeOnCreate. Returns a local
 * reference; duplicate starts fail. State resets when the image is rebound.
 * Android UI, asset provisioning and lifecycle teardown are not implemented. */
int ds_imports_start_activity(uint32_t *activity);

#endif
