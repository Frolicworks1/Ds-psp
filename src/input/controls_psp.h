#ifndef DSPSP_CONTROLS_PSP_H
#define DSPSP_CONTROLS_PSP_H
#include "controls.h"
int ds_controls_psp_init(void);
int ds_controls_psp_poll(DsControls *state,unsigned width,unsigned height,int focused,DsInputBatch *batch);
#endif
