#ifndef DSPSP_RELOC_H
#define DSPSP_RELOC_H

#include "arm_loader.h"

int ds_arm_apply_relocations(const char *path, DsArmImage *image);

#endif
