#ifndef DSPSP_ARMV6_H
#define DSPSP_ARMV6_H

#include <stdint.h>

typedef struct DsArmv6Cpu {
    uint32_t r[16];
    uint32_t cpsr;
    uint32_t pc;
    uint32_t steps;
    uint32_t thumb;
    /* VFPv2 S0-S31; Dn aliases S(2n), S(2n+1), low word first. */
    uint32_t vfp[32];
    uint32_t fpscr;
} DsArmv6Cpu;

typedef struct {
    uint8_t *base;
    /* Total mapped bytes, including any guest heap/stack. */
    uint32_t size;
    uint32_t load_addr;
} DsArmv6Image;

void ds_armv6_reset(DsArmv6Cpu *cpu, uint32_t entry);
int ds_armv6_step(DsArmv6Cpu *cpu, const DsArmv6Image *image);
int ds_armv6_run(DsArmv6Cpu *cpu, const DsArmv6Image *image, uint32_t max_steps);
int ds_armv6_selftest(void);

#endif
