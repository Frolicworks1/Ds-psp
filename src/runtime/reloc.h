#ifndef DSPSP_RELOC_H
#define DSPSP_RELOC_H

#include "arm_loader.h"

int ds_arm_apply_relocations(const char *path, DsArmImage *image);

/* Resolve a defined public function from ELF dynamic symbols, retaining guest
 * virtual addresses. Reject non-executable/out-of-image targets. */
int ds_arm_find_export(const char *path,const DsArmImage *image,const char *name,uint32_t *address);

#endif
