#ifndef DSPSP_IMPORTS_H
#define DSPSP_IMPORTS_H

#include <stdint.h>
struct DsArmv6Cpu;

#define DS_IMPORT_TRAMP_BASE 0xF0000000u

void ds_imports_init(void);
void *ds_import_resolve(const char *library, const char *symbol);
unsigned ds_import_resolve_count(void);
int ds_import_is_trampoline(uint32_t guest_addr);
int ds_import_call(uint32_t guest_addr, struct DsArmv6Cpu *cpu);

#endif
