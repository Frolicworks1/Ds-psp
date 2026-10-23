#include "../src/runtime/imports.h"
#include "../src/cpu/armv6.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static unsigned char mem[512*1024];
static uint32_t env;
static DsArmv6Cpu cpu;
static uint32_t word(uint32_t p){uint32_t v;memcpy(&v,mem+p-0x1000,4);return v;}
static int call(unsigned slot,uint32_t a,uint32_t b,uint32_t c,uint32_t buffer) {
 cpu=(DsArmv6Cpu){0};cpu.r[0]=env;cpu.r[1]=a;cpu.r[2]=b;cpu.r[3]=c;
 cpu.r[13]=0x1800;cpu.r[14]=0x1234;memcpy(mem+0x800,&buffer,4);
 return ds_import_call(word(word(env)+slot*4),&cpu);
}
int main(void) {
 DsArmImage im={.image=mem,.image_base=0x1000,.image_size=4096,.heap_base=0x2000,
 .heap_size=sizeof(mem)-4096,.bridge_size=sizeof(mem)-4096};uint32_t vm;
 ds_imports_init();ds_imports_bind_image(&im);CHECK(!ds_imports_prepare_jni(&vm,&env));
 CHECK(call(176,65537,0,0,0)==-32 && call(176,UINT32_MAX,0,0,0)==-32);
 CHECK(!call(176,8,0,0,0));uint32_t a=cpu.r[0];
 CHECK(!call(171,a,0,0,0) && cpu.r[0]==8);
 memset(mem+0x900,0xff,16);CHECK(!call(200,a,0,8,0x1900));
 for(unsigned i=0;i<8;++i)CHECK(mem[0x900+i]==0);
 memcpy(mem+0x900,"a\0bc",4);CHECK(!call(208,a,2,4,0x1900));
 memset(mem+0x900,0xff,8);CHECK(!call(200,a,0,8,0x1900));
 CHECK(!memcmp(mem+0x900,"\0\0a\0bc\0\0",8));
 CHECK(call(208,a,7,2,0x1900)==-31 && call(200,a,UINT32_MAX,1,0x1900)==-31);
 CHECK(call(200,a,0,1,0xffffffff)==-31 && call(171,0,0,0,0)==-31);
 CHECK(!call(200,a,8,0,0));
 CHECK(!call(21,a,0,0,0));uint32_t global=cpu.r[0];
 CHECK(!call(23,a,0,0,0));CHECK(!call(171,global,0,0,0) && cpu.r[0]==8);
 CHECK(!call(22,global,0,0,0));unsigned used=im.heap_used;
 CHECK(!call(176,8,0,0,0));a=cpu.r[0];CHECK(im.heap_used==used);
 CHECK(!call(200,a,0,8,0x1900));for(unsigned i=0;i<8;++i)CHECK(!mem[0x900+i]);
 uint32_t refs[4]={a};
 for(unsigned i=1;i<4;++i){CHECK(!call(176,65536,0,0,0));refs[i]=cpu.r[0];}
 CHECK(call(176,1,0,0,0)==-32);
 for(unsigned i=0;i<4;++i)CHECK(!call(23,refs[i],0,0,0));
 used=im.heap_used;CHECK(!call(176,0,0,0,0));a=cpu.r[0];
 CHECK(!call(171,a,0,0,0) && !cpu.r[0] && im.heap_used==used);
 ds_imports_bind_image(NULL);ds_imports_bind_image(&im);im.heap_used=0;
 CHECK(!ds_imports_prepare_jni(&vm,&env));
 im.bridge_size=im.heap_used;CHECK(call(176,1,0,0,0)==-32);
 im.bridge_size=im.heap_size;CHECK(!call(176,1,0,0,0));
 puts("JNI byte arrays: bounds, zeroing, regions, reference lifetime, capacity and allocation retry passed");
}
