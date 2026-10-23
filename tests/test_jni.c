#define _POSIX_C_SOURCE 200809L
#include <unistd.h>
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
 CHECK(!call(219,0x1380,0,0) && !cpu.r[0] && read32(0x1380)==vm);
 CHECK(call(219,0x2fff,0,0)==-31 && call(219,0x1381,0,0)==-31);
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
 strcpy((char*)mem+384,"com/ea/blast/MainActivity");
 strcpy((char*)mem+448,"GetInstance");strcpy((char*)mem+512,"()Lcom/ea/blast/MainActivity;");
 strcpy((char*)mem+576,"getAssets");strcpy((char*)mem+640,"()Landroid/content/res/AssetManager;");
 CHECK(!call(6,0x1180,0,0));uint32_t activity=cpu.r[0];
 CHECK(!call(6,0x1000,0,0));uint32_t eaio=cpu.r[0];
 CHECK(!call(24,activity,eaio,0) && !cpu.r[0]);
 CHECK(!call(113,activity,0x11c0,0x1200));uint32_t instanceMethod=cpu.r[0];
 CHECK(instanceMethod!=startup && instanceMethod!=shutdown);
 CHECK(!call(33,activity,0x1240,0x1280));uint32_t assetsMethod=cpu.r[0];
 CHECK(assetsMethod!=instanceMethod && assetsMethod!=startup && assetsMethod!=shutdown);
 CHECK(call(113,activity,0x1240,0x1280)==-32); /* getAssets is not static */
 CHECK(call(33,activity,0x11c0,0x1200)==-32); /* GetInstance is static */
 CHECK(call(113,eaio,0x11c0,0x1200)==-32); /* wrong declaring class */
 CHECK(call(33,eaio,0x1240,0x1280)==-32);
 CHECK(call(113,activity,0x11c0,0x1280)==-32); /* wrong return type */
 for(unsigned slot=114;slot<=116;++slot)
  CHECK(!call(slot,activity,instanceMethod,0xffffffff) && !cpu.r[0]);
 uint32_t receiver=99;CHECK(ds_imports_start_activity(NULL)==-31);
 CHECK(!ds_imports_start_activity(&receiver) && receiver);
 uint32_t duplicate=99;CHECK(ds_imports_start_activity(&duplicate)==-31 && !duplicate);
 for(unsigned slot=114;slot<=116;++slot) {
  CHECK(!call(slot,activity,instanceMethod,0xffffffff));uint32_t copy=cpu.r[0];
  CHECK(copy!=receiver && !call(24,copy,receiver,0) && cpu.r[0]);
  CHECK(!call(24,copy,activity,0) && !cpu.r[0]);
  CHECK(!call(23,copy,0,0));
 }
 CHECK(call(115,eaio,instanceMethod,0)==-32);
 CHECK(call(115,receiver,instanceMethod,0)==-32);
 CHECK(call(115,activity,assetsMethod,0)==-32);
 CHECK(call(115,0,instanceMethod,0)==-31);
 CHECK(call(35,receiver,assetsMethod,0)==-32); /* assets not provisioned */
 char assetRoot[]="/tmp/ds-jni-assets-XXXXXX";CHECK(mkdtemp(assetRoot));
 CHECK(ds_imports_mount_assets("/does-not-exist/ds-assets")==-1);
 CHECK(!ds_imports_mount_assets(assetRoot) && ds_imports_mount_assets(assetRoot)==-1);
 uint32_t manager=0;
 for(unsigned slot=34;slot<=36;++slot) {
  CHECK(!call(slot,receiver,assetsMethod,0xffffffff));uint32_t ref=cpu.r[0];
  CHECK(!call(24,ref,receiver,0) && !cpu.r[0]);
  if (!manager) {CHECK(!call(21,ref,0,0));manager=cpu.r[0];}
  CHECK(!call(24,ref,manager,0) && cpu.r[0]);
  CHECK(!call(23,ref,0,0));
 }
 CHECK(call(35,activity,assetsMethod,0)==-32);
 CHECK(call(35,receiver,instanceMethod,0)==-32);
 CHECK(call(35,0,assetsMethod,0)==-31);
 CHECK(ds_imports_bind_eaio_startup(0)==-31);
 CHECK(ds_imports_bind_eaio_startup(0x1002)==-31);
 CHECK(ds_imports_bind_eaio_startup(0x2000)==-31); /* bridge is not code */
 CHECK(ds_imports_bind_eaio_startup(0x0fff)==-31);
 CHECK(!ds_imports_bind_eaio_startup(0x1800));
 CHECK(call(142,eaio,startup,0x2fff)==-31);
 CHECK(call(142,eaio,startup,0x1301)==-31);
 CHECK(call(141,activity,startup,manager)==-32);
 CHECK(call(141,eaio,shutdown,manager)==-32);
 CHECK(call(141,eaio,startup,receiver)==-31);
 for(unsigned thumb=0;thumb<2;++thumb) {
  CHECK(!ds_imports_bind_eaio_startup(0x1800+thumb));
  for(unsigned slot=141;slot<=143;++slot) {
   memcpy(mem+768,&manager,4);memset(mem+772,0xff,4);
   cpu=(DsArmv6Cpu){0};cpu.r[0]=env;cpu.r[1]=eaio;cpu.r[2]=startup;
   cpu.r[3]=slot==141?manager:0x1300;cpu.r[13]=0x1f00;cpu.r[14]=0x4001;
   for(unsigned i=4;i<=11;++i)cpu.r[i]=0x12340000+i;
   CHECK(!ds_import_call(read32(read32(env)+slot*4),&cpu));
   CHECK(cpu.pc==0x1800 && cpu.thumb==thumb && cpu.r[0]==env && cpu.r[1]==eaio && cpu.r[2]==manager);
   CHECK(cpu.r[13]==0x1f00 && cpu.r[14]==0x4001);
   for(unsigned i=4;i<=11;++i)CHECK(cpu.r[i]==0x12340000+i);
   uint32_t code=thumb?0x4770:0xe12fff1e; /* BX LR */
   memcpy(mem+0x800,&code,4);
   DsArmv6Image memory={mem,sizeof(mem),0x1000};
   CHECK(!ds_armv6_step(&cpu,&memory));
   CHECK(cpu.pc==0x4000 && cpu.thumb && cpu.r[13]==0x1f00);
  }
 }
 CHECK(!call(31,manager,0,0));uint32_t managerClass=cpu.r[0];
 strcpy((char*)mem+1024,"android/content/res/AssetManager");
 CHECK(!call(6,0x1400,0,0));uint32_t foundClass=cpu.r[0];
 CHECK(!call(24,managerClass,foundClass,0) && cpu.r[0]);
 CHECK(!call(23,foundClass,0,0) && !call(23,managerClass,0,0));
 CHECK(!call(31,receiver,0,0));foundClass=cpu.r[0];
 CHECK(!call(24,foundClass,activity,0) && cpu.r[0]);
 CHECK(!call(23,foundClass,0,0));
 CHECK(call(31,0,0,0)==-31 && call(31,activity,0,0)==-32);
 const struct {const char *cls,*name,*sig;} methods[]={
  {"java/io/InputStream","read","([BII)I"},
  {"java/io/InputStream","skip","(J)J"},
  {"java/io/InputStream","close","()V"},
  {"android/content/res/AssetManager","open","(Ljava/lang/String;)Ljava/io/InputStream;"},
  {"android/content/res/AssetManager","openFd","(Ljava/lang/String;)Landroid/content/res/AssetFileDescriptor;"},
  {"android/content/res/AssetManager","list","(Ljava/lang/String;)[Ljava/lang/String;"},
  {"android/content/res/AssetFileDescriptor","getLength","()J"}
 };
 uint32_t methodIds[7];
 for(unsigned j=0;j<7;++j) {
  strcpy((char*)mem+1024,methods[j].cls);strcpy((char*)mem+1152,methods[j].name);
  strcpy((char*)mem+1280,methods[j].sig);
  CHECK(!call(6,0x1400,0,0));uint32_t cls=cpu.r[0];
  CHECK(!call(33,cls,0x1480,0x1500));methodIds[j]=cpu.r[0];
  for(unsigned k=0;k<j;++k)CHECK(methodIds[k]!=methodIds[j]);
  CHECK(call(113,cls,0x1480,0x1500)==-32);
  CHECK(call(33,eaio,0x1480,0x1500)==-32);
  strcpy((char*)mem+1280,"()Z");CHECK(call(33,cls,0x1480,0x1500)==-32);
  CHECK(!call(23,cls,0,0));
 }
 /* JNI UTF strings used by Java/platform metadata queries. */
 strcpy((char*)mem+1024,"java/lang/String");
 strcpy((char*)mem+1536,"Device-Model");
 CHECK(!call(6,0x1400,0,0));uint32_t stringClass=cpu.r[0];
 CHECK(!call(167,0x1600,0,0));uint32_t stringRef=cpu.r[0];
 CHECK(stringRef && !call(168,stringRef,0,0) && cpu.r[0]==11);
 CHECK(!call(169,stringRef,0x1640,0));uint32_t stringChars=cpu.r[0];
 CHECK(stringChars && !strcmp((char*)mem+stringChars-0x1000,"Device-Model"));
 CHECK(read32(0x1640)==1);
 CHECK(!call(170,stringRef,stringChars,0) && cpu.r[0]==0);
 CHECK(call(170,stringRef,stringChars+1,0)==-31);
 CHECK(!call(31,stringRef,0,0));uint32_t actualStringClass=cpu.r[0];
 CHECK(!call(24,stringClass,actualStringClass,0) && cpu.r[0]);
 CHECK(!call(23,actualStringClass,0,0) && !call(23,stringRef,0,0));
 CHECK(!call(23,stringClass,0,0));
 CHECK(!call(22,manager,0,0));
 CHECK(!rmdir(assetRoot));
 CHECK(!call(23,receiver,0,0)); /* singleton survives handle deletion */
 CHECK(!call(115,activity,instanceMethod,0));receiver=cpu.r[0];
 CHECK(receiver && !call(23,receiver,0,0));
 CHECK(call(21,assetsMethod,0,0)==-31);
 CHECK(!call(23,activity,0,0) && !call(23,eaio,0,0));
 uint32_t refs[64];
 for(unsigned i=0;i<64;++i){CHECK(!call(6,0x1000,0,0));refs[i]=cpu.r[0];}
 CHECK(call(6,0x1000,0,0)==-32 && im.heap_used==used);
 /* A full local-reference table also prevents GetInstance results. */
 CHECK(!call(23,refs[0],0,0));
 CHECK(!call(6,0x1180,0,0));activity=cpu.r[0];
 CHECK(call(115,activity,instanceMethod,0)==-32);
 for(unsigned i=0;i<64;++i)CHECK(!call(23,refs[i],0,0));
 CHECK(!call(6,0x1000,0,0) && im.heap_used==used);
 CHECK(call(141,cpu.r[0],startup,0)==-31); /* null asset argument rejected */
 cpu.r[0]=vm;CHECK(ds_import_call(version,&cpu)==-31);
 ds_imports_bind_image(NULL);CHECK(!ds_import_is_trampoline(getenv));
 CHECK(ds_imports_start_activity(&receiver)==-31 && !receiver);
 CHECK(ds_imports_bind_eaio_startup(0x1800)==-31);
 ds_imports_bind_image(&im);im.heap_used=0;
 CHECK(!ds_imports_prepare_jni(&vm,&env));
 CHECK(!call(6,0x1180,0,0));activity=cpu.r[0];
 CHECK(!call(113,activity,0x11c0,0x1200));instanceMethod=cpu.r[0];
 CHECK(!call(115,activity,instanceMethod,0) && !cpu.r[0]);
 for(unsigned i=0;i<63;++i){CHECK(!call(6,0x1000,0,0));refs[i]=cpu.r[0];}
 receiver=99;CHECK(ds_imports_start_activity(&receiver)==-32 && !receiver);
 CHECK(!call(115,activity,instanceMethod,0) && !cpu.r[0]);
 CHECK(!call(23,refs[0],0,0));
 CHECK(!ds_imports_start_activity(&receiver)); /* allocation failure is retryable */
 CHECK(call(35,receiver,assetsMethod,0)==-32); /* mount reset with image */
 ds_imports_bind_image(NULL);ds_imports_bind_image(&im);im.heap_used=im.bridge_size-8;
 CHECK(ds_imports_prepare_jni(&vm,&env)==-1 && !vm && !env && im.heap_used==im.bridge_size-8);
 puts("JNI tables, class/method identity, local/global lifetimes, capacity and bounds passed");return 0;
}
