#include "../src/input/controls.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static DsControls state;
static DsInputBatch b;
static void update(uint32_t buttons,unsigned x,unsigned y,int focus) {
 CHECK(!ds_controls_update(&state,(DsPad){buttons,(uint8_t)x,(uint8_t)y},480,272,focus,&b));
 CHECK(b.count<=DS_INPUT_MAX_EVENTS);
}
int main(void) {
 update(0,128,128,1);CHECK(!b.count);
 update(0,104,152,1);CHECK(!b.count); /* deadzone includes boundary */
 update(0,255,128,1);CHECK(b.count==2 && b.events[0].type==DS_INPUT_TOUCH_DOWN && b.events[1].x>b.events[0].x);
 float right=b.events[1].x;update(0,255,128,1);CHECK(b.count==1 && b.events[0].x==right);
 update(0,128,128,1);CHECK(b.count==1 && b.events[0].type==DS_INPUT_TOUCH_UP && b.events[0].x==right);
 update(DS_PAD_TRIANGLE,128,128,1);CHECK(b.count==3 && b.events[1].y>b.events[0].y && b.events[2].type==DS_INPUT_TOUCH_UP);
 update(DS_PAD_TRIANGLE|DS_PAD_CROSS|DS_PAD_SQUARE|DS_PAD_CIRCLE,128,128,1);CHECK(!b.count);
 update(DS_PAD_CIRCLE,128,128,1);CHECK(b.count==3 && b.events[1].x>b.events[0].x);
 update(DS_PAD_L|DS_PAD_R|DS_PAD_UP,128,128,1);CHECK(b.count==3 && b.events[2].code==23);
 update(DS_PAD_L|DS_PAD_R|DS_PAD_UP,128,128,1);CHECK(!b.count); /* no repeated keydown */
 update(DS_PAD_L|DS_PAD_R|DS_PAD_UP|DS_PAD_SELECT,128,128,1);
 CHECK(b.count==2 && b.events[0].type==DS_INPUT_KEY_UP && b.events[0].code==23 && b.events[1].code==100);
 update(0,128,128,1);CHECK(b.count==3); /* modifier use must not toggle HUD */
 update(DS_PAD_SELECT,128,128,1);CHECK(!b.count);
 update(0,128,128,1);CHECK(b.count==2 && b.events[0].code==109 && b.events[1].type==DS_INPUT_KEY_UP);
 update(DS_PAD_R|DS_PAD_SELECT,0,0,1);CHECK(state.moving);
 update(DS_PAD_R|DS_PAD_SELECT,0,0,0);CHECK(!state.moving && !state.held_keys && !state.select_pending);
 CHECK(b.count==2 && b.events[0].type==DS_INPUT_KEY_UP && b.events[1].type==DS_INPUT_TOUCH_UP);
 const char *name;uint32_t words[7];DsInputEvent e={DS_INPUT_TOUCH_DOWN,1000,1,96.0f,37.5f};
 CHECK(ds_controls_pack(&e,0x1234,0,&name,words)==7 && words[0]==0x1234 && words[2]==0x6000c && words[3]==1000 && words[4]==1);
 CHECK(words[5]==0x42c00000 && words[6]==0x42160000 && strstr(name,"PointerEvent"));
 e=(DsInputEvent){DS_INPUT_KEY_DOWN,600,102,0,0};
 CHECK(ds_controls_pack(&e,1,2,&name,words)==5 && words[2]==600 && words[3]==102 && words[4]==1 && strstr(name,"KeyDown"));
 /* Exercise modifier transitions and all physical button combinations. */
 const unsigned masks[]={1,8,16,32,64,128,256,512,4096,8192,16384,32768};
 for(unsigned bits=0;bits<4096;++bits) {
  uint32_t pad=0;for(unsigned i=0;i<12;++i)if(bits&(1u<<i))pad|=masks[i];
  update(pad,(bits&1)?0:255,(bits&2)?0:255,1);
  CHECK(b.count<=DS_INPUT_MAX_EVENTS);
 }
 CHECK(ds_controls_update(&state,(DsPad){0},0,272,1,&b)==-1);
 puts("PSP movement/camera, modifiers, releases, all button combinations and JNI packing passed");return 0;
}
