#include "controls_psp.h"
#include <pspctrl.h>
_Static_assert((unsigned)DS_PAD_CROSS==PSP_CTRL_CROSS && (unsigned)DS_PAD_CIRCLE==PSP_CTRL_CIRCLE &&
 (unsigned)DS_PAD_SQUARE==PSP_CTRL_SQUARE && (unsigned)DS_PAD_TRIANGLE==PSP_CTRL_TRIANGLE &&
 (unsigned)DS_PAD_L==PSP_CTRL_LTRIGGER && (unsigned)DS_PAD_R==PSP_CTRL_RTRIGGER &&
 (unsigned)DS_PAD_SELECT==PSP_CTRL_SELECT && (unsigned)DS_PAD_START==PSP_CTRL_START &&
 (unsigned)DS_PAD_UP==PSP_CTRL_UP && (unsigned)DS_PAD_DOWN==PSP_CTRL_DOWN &&
 (unsigned)DS_PAD_LEFT==PSP_CTRL_LEFT && (unsigned)DS_PAD_RIGHT==PSP_CTRL_RIGHT,
 "control mask must match PSP SDK");
int ds_controls_psp_init(void) {
 if(sceCtrlSetSamplingCycle(0)<0)return -1;
 return sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG)<0?-1:0;
}
int ds_controls_psp_poll(DsControls *state,unsigned w,unsigned h,int focused,DsInputBatch *batch) {
 SceCtrlData data={0};DsPad pad={0,128,128};
 if(sceCtrlPeekBufferPositive(&data,1)>0)pad=(DsPad){data.Buttons,data.Lx,data.Ly};
 else focused=0; /* release held events if the input source disappears */
 return ds_controls_update(state,pad,w,h,focused,batch);
}
