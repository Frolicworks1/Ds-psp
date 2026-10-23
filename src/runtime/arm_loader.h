#ifndef DSPSP_ARM_LOADER_H
#define DSPSP_ARM_LOADER_H

#include <stdint.h>

typedef struct {
    uint8_t *image;
    uint32_t image_base;
    uint32_t image_size;
    uint32_t entry;
    uint32_t load_segments;
    uint32_t heap_base;
    uint32_t heap_size;
    uint32_t heap_used;
    /* heap_size remains the total mapped arena. Allocation pools are disjoint. */
    uint32_t bridge_size;
    uint32_t brk_base, brk_size, brk_used;
    uint32_t stack_base, stack_size;
    uint32_t mmap_base, mmap_size;
} DsArmImage;

int ds_arm_loader_load(const char *path, DsArmImage *out);
void ds_arm_loader_unload(DsArmImage *image);
const uint8_t *ds_arm_loader_ptr(const DsArmImage *image, uint32_t vaddr);

#endif
