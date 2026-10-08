#ifndef DSPSP_ARM_ELF_H
#define DSPSP_ARM_ELF_H

#include <stdint.h>

typedef struct {
    uint32_t entry;
    uint32_t image_base;
    uint32_t image_size;
    uint32_t load_segments;
} DsArmElfInfo;

int ds_arm_elf_probe(const char *path, DsArmElfInfo *info);

#endif
