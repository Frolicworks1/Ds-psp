#include "controls.h"
#include <string.h>
/* Android keyboard actions as used by the original game's keyboard module. */
static const uint32_t keys[]={102,103,23,99,21,22,100,4,19,20,108};
static void event(DsInputBatch *b,DsInputType type,uint32_t module,uint32_t code,float x,float y) {
 b->events[b->count++]=(DsInputEvent){type,module,code,x,y};
}
static float axis(unsigned raw) {
 int d=(int)raw-128;
 if(d>=-24 && d<=24)return 0;
 return d>0 ? (float)(d-24)/103.0f : (float)(d+24)/104.0f;
}
int ds_controls_update(DsControls *s,DsPad pad,unsigned w,unsigned h,int focus,DsInputBatch *b) {
 if(!s || !b || !w || !h || w>4096 || h>4096)return -1;
 b->count=0;
 uint32_t p=focus?pad.buttons:0,desired=0;
 int modifier=!!(p&DS_PAD_SELECT);
 if(modifier && !s->select_pending) {s->select_pending=1;s->select_used=0;}
 if(modifier && (p&~DS_PAD_SELECT))s->select_used=1;
 if(p&DS_PAD_L)desired|=1u<<0;
 if(p&DS_PAD_R)desired|=1u<<1;
 if(p&DS_PAD_UP)desired|=1u<<(modifier?6:2);
 if(p&DS_PAD_DOWN)desired|=1u<<(modifier?7:3);
 if(p&DS_PAD_LEFT)desired|=1u<<(modifier?8:4);
 if(p&DS_PAD_RIGHT)desired|=1u<<(modifier?9:5);
 if(p&DS_PAD_START)desired|=1u<<10;
 /* Release old bindings before pressing replacement bindings, including a
  * modifier pressed or released while an action button remains held. */
 for(unsigned i=0;i<sizeof(keys)/sizeof(*keys);++i)
  if((s->held_keys&~desired)&(1u<<i))event(b,DS_INPUT_KEY_UP,600,keys[i],0,0);
 for(unsigned i=0;i<sizeof(keys)/sizeof(*keys);++i)
  if((desired&~s->held_keys)&(1u<<i))event(b,DS_INPUT_KEY_DOWN,600,keys[i],0,0);
 s->held_keys=desired;
 if(!modifier && s->select_pending) {
  if(focus && !s->select_used) {
   event(b,DS_INPUT_KEY_DOWN,600,109,0,0);event(b,DS_INPUT_KEY_UP,600,109,0,0);
  }
  s->select_pending=s->select_used=0;
 }
 float x=focus?axis(pad.x):0,y=focus?axis(pad.y):0;
 /* Guest coordinates follow the game's landscape touch input profile. These
  * anchors need on-device tuning once rendering/lifecycle is functional. */
 float anchor_x=(float)w*0.2f,anchor_y=(float)h*(75.0f/544.0f);
 if(x || y) {
  if(!s->moving)event(b,DS_INPUT_TOUCH_DOWN,1000,1,anchor_x,anchor_y);
  s->last_x=anchor_x+x*(float)w*(75.0f/960.0f);
  s->last_y=anchor_y+y*(float)h*(75.0f/544.0f);
  event(b,DS_INPUT_TOUCH_MOVE,1000,1,s->last_x,s->last_y);s->moving=1;
 } else if(s->moving) {
  event(b,DS_INPUT_TOUCH_UP,1000,1,s->last_x,s->last_y);s->moving=0;
 }
 int camera_x=!!(p&DS_PAD_CIRCLE)-!!(p&DS_PAD_SQUARE);
 int camera_y=!!(p&DS_PAD_CROSS)-!!(p&DS_PAD_TRIANGLE);
 if(camera_x || camera_y) {
  float cx=(float)w*(786.0f/960.0f),cy=(float)h*(180.0f/544.0f);
  event(b,DS_INPUT_TOUCH_DOWN,1100,2,cx,cy);
  cx+=(float)camera_x*(float)w*(155.0f/960.0f);
  cy-=(float)camera_y*(float)h*(105.0f/544.0f);
  event(b,DS_INPUT_TOUCH_MOVE,1100,2,cx,cy);
  event(b,DS_INPUT_TOUCH_UP,1100,2,cx,cy);
 }
 return 0;
}
unsigned ds_controls_pack(const DsInputEvent *e,uint32_t env,uint32_t object,
                          const char **name,uint32_t words[7]) {
 if(!e || !name || !words)return 0;
 memset(words,0,7*sizeof(*words));words[0]=env;words[1]=object;
 if(e->type==DS_INPUT_KEY_DOWN || e->type==DS_INPUT_KEY_UP) {
  *name=e->type==DS_INPUT_KEY_DOWN?"Java_com_ea_blast_KeyboardAndroid_NativeOnKeyDown":
                                   "Java_com_ea_blast_KeyboardAndroid_NativeOnKeyUp";
  words[2]=e->module;words[3]=e->code;words[4]=1;return 5;
 }
 if(e->type<DS_INPUT_TOUCH_DOWN || e->type>DS_INPUT_TOUCH_UP)return 0;
 *name="Java_com_ea_blast_TouchSurfaceAndroid_NativeOnPointerEvent";
 /* IDs verified from this donor's NativeGetIdRawPointer* exports. */
 static const uint32_t raw_ids[]={0x6000c,0x4000c,0x8000c};
 words[2]=raw_ids[e->type-DS_INPUT_TOUCH_DOWN];
 words[3]=e->module;words[4]=e->code;
 _Static_assert(sizeof(float)==4,"guest JNI requires IEEE binary32 floats");
 memcpy(words+5,&e->x,4);memcpy(words+6,&e->y,4);return 7;
}
