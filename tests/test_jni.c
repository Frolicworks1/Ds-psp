#include "../src/runtime/imports.h"
#include "../src/cpu/armv6.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static unsigned char mem[8192];
static uint32_t vm,env;
static DsArmv6Cpu cpu;
static uint32_t read32(uint32_t a){uint32_t v;CHECK(a>=0x1000 && a<=0x2ffc);memcpy(&v,mem+a-0x1000,4);return v;}
static int call(unsigned slot,uint32_t a,uint32_t b,uint32_t c) {
 cpu=(DsArmv6Cpu){0};cpu.r[0]=env;cpu.r[1]=a;cpu.r[2]=b;cpu.r[3]=c;cpu.r[14]=0x4001;cpu.pc=0x5000;
 int rc=ds_import_call(read32(read32(env)+slot*4),&cpu);
 CHECK(rc?cpu.pc==0x5000:cpu.pc==0x4000 && cpu.thumb);return rc;
}
int main(void) {
 DsArmImage im={.image=mem,.image_base=0x1000,.image_size=4096,.heap_base=0x2000,.heap_size=4096,.bridge_size=4096};
 uint32_t vm2,env2;ds_imports_init();CHECK(ds_imports_prepare_jni(&vm,&env)==-1);
 ds_imports_bind_image(&im);CHECK(!ds_imports_prepare_jni(&vm,&env));
 unsigned used=im.heap_used;CHECK(!ds_imports_prepare_jni(&vm2,&env2) && vm==vm2 && env==env2 && im.heap_used==used);
 uint32_t table=read32(vm),getenv=read32(table+6*4),version=read32(read32(env)+4*4);
 CHECK(read32(table)==0 && read32(read32(env))==0 && ds_import_is_trampoline(getenv));
 cpu=(DsArmv6Cpu){0};cpu.r[0]=vm;cpu.r[1]=0x1000;cpu.r[2]=0x10004;cpu.r[14]=0x4001;
 CHECK(!ds_import_call(getenv,&cpu) && cpu.r[0]==0 && read32(0x1000)==env && cpu.pc==0x4000 && cpu.thumb);
 cpu.r[0]=vm;cpu.r[2]=42;CHECK(!ds_import_call(getenv,&cpu) && cpu.r[0]==(uint32_t)-3 && read32(0x1000)==0);
 cpu.r[0]=vm;cpu.r[1]=0x2fff;CHECK(ds_import_call(getenv,&cpu)==-31);
 CHECK(!call(4,0,0,0) && cpu.r[0]==0x10006);
 strcpy((char*)mem,"com/ea/EAIO/EAIO");
 strcpy((char*)mem+64,"Startup");strcpy((char*)mem+128,"(Landroid/content/res/AssetManager;)V");
 strcpy((char*)mem+192,"Shutdown");strcpy((char*)mem+256,"()V");
 CHECK(!call(6,0x1000,0,0));uint32_t local=cpu.r[0];
 CHECK(!call(232,local,0,0) && cpu.r[0]==1);
 CHECK(!call(21,local,0,0));uint32_t global=cpu.r[0];CHECK(global!=local);
 CHECK(!call(232,global,0,0) && cpu.r[0]==2);
 CHECK(!call(24,local,global,0) && cpu.r[0]==1);
 CHECK(!call(24,local,0,0) && cpu.r[0]==0);
 CHECK(!call(24,0,0,0) && cpu.r[0]==1);
 CHECK(call(22,local,0,0)==-31); /* wrong reference kind must not delete it */
 CHECK(!call(113,local,0x1040,0x1080));uint32_t startup=cpu.r[0];
 CHECK(!call(113,global,0x1040,0x1080) && cpu.r[0]==startup);
 CHECK(!call(113,local,0x10c0,0x1100));uint32_t shutdown=cpu.r[0];CHECK(startup!=shutdown);
 CHECK(call(113,local,0x1040,0x1100)==-32); /* exact signature required */
 CHECK(call(113,0x1234,0x1040,0x1080)==-31);
 CHECK(call(113,local,0x1040,0x3000)==-31);
 CHECK(call(21,startup,0,0)==-31); /* method IDs are not object handles */
 CHECK(!call(23,local,0,0));
 CHECK(call(21,local,0,0)==-31 && !call(232,local,0,0) && cpu.r[0]==0);
 CHECK(!call(25,global,0,0));local=cpu.r[0]; /* independent local lifetime */
 CHECK(!call(22,global,0,0));
 CHECK(!call(113,local,0x1040,0x1080) && cpu.r[0]==startup);
 CHECK(!call(23,local,0,0));
 CHECK(!call(21,0,0,0) && !cpu.r[0]);CHECK(!call(25,0,0,0) && !cpu.r[0]);
 CHECK(!call(22,0,0,0) && !call(23,0,0,0));
 strcpy((char*)mem+320,"unknown/Class");CHECK(call(6,0x1140,0,0)==-32);
 CHECK(call(6,0x3000,0,0)==-31);
 memset(mem+8190,'x',2); /* unterminated to end of mapped space */
 CHECK(call(6,0x2ffe,0,0)==-31);
 uint32_t refs[64];
 for(unsigned i=0;i<64;++i){CHECK(!call(6,0x1000,0,0));refs[i]=cpu.r[0];}
 CHECK(call(6,0x1000,0,0)==-32 && im.heap_used==used);
 for(unsigned i=0;i<64;++i)CHECK(!call(23,refs[i],0,0));
 CHECK(!call(6,0x1000,0,0) && im.heap_used==used);
 CHECK(call(141,cpu.r[0],startup,0)==-32); /* lookup is not method execution */
 cpu.r[0]=vm;CHECK(ds_import_call(version,&cpu)==-31);
 ds_imports_bind_image(NULL);CHECK(!ds_import_is_trampoline(getenv));
 ds_imports_bind_image(&im);im.heap_used=im.bridge_size-8;
 CHECK(ds_imports_prepare_jni(&vm,&env)==-1 && !vm && !env && im.heap_used==im.bridge_size-8);
 puts("JNI tables, class/method identity, local/global lifetimes, capacity and bounds passed");return 0;
}
