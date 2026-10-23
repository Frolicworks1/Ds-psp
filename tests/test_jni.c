#include "../src/runtime/imports.h"
#include "../src/cpu/armv6.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static unsigned char mem[8192];
static uint32_t read32(uint32_t a){uint32_t v;CHECK(a>=0x1000 && a<=0x2ffc);memcpy(&v,mem+a-0x1000,4);return v;}
int main(void) {
 DsArmImage im={.image=mem,.image_base=0x1000,.image_size=4096,.heap_base=0x2000,.heap_size=4096,.bridge_size=4096};
 uint32_t vm,env,vm2,env2;ds_imports_init();CHECK(ds_imports_prepare_jni(&vm,&env)==-1);
 ds_imports_bind_image(&im);CHECK(!ds_imports_prepare_jni(&vm,&env));
 unsigned used=im.heap_used;CHECK(!ds_imports_prepare_jni(&vm2,&env2) && vm==vm2 && env==env2 && im.heap_used==used);
 uint32_t table=read32(vm),getenv=read32(table+6*4),version=read32(read32(env)+4*4);
 CHECK(read32(table)==0 && read32(read32(env))==0 && ds_import_is_trampoline(getenv));
 DsArmv6Cpu cpu={0};cpu.r[0]=vm;cpu.r[1]=0x1000;cpu.r[2]=0x10004;cpu.r[14]=0x4001;
 CHECK(!ds_import_call(getenv,&cpu) && cpu.r[0]==0 && read32(0x1000)==env && cpu.pc==0x4000 && cpu.thumb);
 cpu.r[0]=vm;cpu.r[2]=42;CHECK(!ds_import_call(getenv,&cpu) && cpu.r[0]==(uint32_t)-3 && read32(0x1000)==0);
 cpu.r[0]=vm;cpu.r[1]=0x2fff;CHECK(ds_import_call(getenv,&cpu)==-31);
 cpu.r[0]=env;CHECK(!ds_import_call(version,&cpu) && cpu.r[0]==0x10006);
 cpu.r[0]=env;CHECK(ds_import_call(read32(read32(env)+6*4),&cpu)==-32); /* FindClass not faked */
 cpu.r[0]=vm;CHECK(ds_import_call(version,&cpu)==-31);
 ds_imports_bind_image(NULL);CHECK(!ds_import_is_trampoline(getenv));
 ds_imports_bind_image(&im);im.heap_used=im.bridge_size-8;
 CHECK(ds_imports_prepare_jni(&vm,&env)==-1 && !vm && !env && im.heap_used==im.bridge_size-8);
 puts("bounded guest JNI tables, version negotiation, invalid pointers and unsupported calls passed");return 0;
}
