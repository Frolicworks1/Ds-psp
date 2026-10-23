#include "../src/runtime/imports.h"
#include "../src/cpu/armv6.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x); exit(1); } } while (0)
static unsigned char mem[4096];
static DsArmv6Cpu cpu;
static time_t now;
time_t time(time_t *out) { if(out)*out=now; return now; }
static int call(const char *name,uint32_t a,uint32_t b,uint32_t d) {
    cpu=(DsArmv6Cpu){0};cpu.r[0]=a;cpu.r[1]=b;cpu.r[2]=d;cpu.r[14]=0x2001;cpu.pc=0x3000;
    int rc=ds_import_call((uint32_t)(uintptr_t)ds_import_resolve(NULL,name),&cpu);
    if(!rc)CHECK(cpu.pc==0x2000 && cpu.thumb);
    else CHECK(cpu.pc==0x3000);
    return rc;
}
static uint32_t word(unsigned off) {uint32_t v;memcpy(&v,mem+off,4);return v;}
int main(void) {
    DsArmImage im={.image=mem,.image_base=0x1000,.image_size=1024,
        .heap_base=0x1400,.heap_size=3072,.bridge_size=1024};
    ds_imports_init();ds_imports_bind_image(&im);
    CHECK(!call("__cxa_guard_acquire",0x1000,0,0) && cpu.r[0]==1 && word(0)==2);
    CHECK(call("__cxa_guard_acquire",0x1000,0,0)==-32 && word(0)==2);
    CHECK(!call("__cxa_guard_abort",0x1000,0,0) && word(0)==0);
    CHECK(!call("__cxa_guard_acquire",0x1000,0,0) && cpu.r[0]==1);
    CHECK(!call("__cxa_guard_release",0x1000,0,0) && word(0)==1);
    CHECK(!call("__cxa_guard_acquire",0x1000,0,0) && cpu.r[0]==0);
    CHECK(call("__cxa_guard_release",0x1000,0,0)==-32);
    CHECK(call("__cxa_guard_acquire",0x1fff,0,0)==-31);
    CHECK(!call("__atomic_swap",0x12345678,0x1000,0) && cpu.r[0]==1 && word(0)==0x12345678);
    CHECK(call("__atomic_swap",42,0x1001,0)==-31 && word(0)==0x12345678);
    CHECK(!call("__atomic_inc",0x1000,0,0) && cpu.r[0]==0x12345678 && word(0)==0x12345679);
    CHECK(!call("__atomic_dec",0x1000,0,0) && cpu.r[0]==0x12345679 && word(0)==0x12345678);
    CHECK(!call("__atomic_swap",UINT32_MAX,0x1000,0));
    CHECK(!call("__atomic_inc",0x1000,0,0) && cpu.r[0]==UINT32_MAX && word(0)==0);
    CHECK(!call("__atomic_dec",0x1000,0,0) && cpu.r[0]==0 && word(0)==UINT32_MAX);
    CHECK(call("__atomic_inc",0x1fff,0,0)==-31);
    CHECK(!call("pthread_self",0,0,0) && cpu.r[0]==1);
    CHECK(!call("pthread_equal",1,1,0) && cpu.r[0]==1);
    CHECK(!call("pthread_equal",1,2,0) && cpu.r[0]==0);
    CHECK(!call("sem_init",0x1000,0,0x3fffffff) && word(0)==0x7ffffffe);
    CHECK(call("sem_init",0x1000,1,0)==-32 && word(0)==0x7ffffffe);
    CHECK(!call("sem_init",0x1000,0,0x40000000) && cpu.r[0]==UINT32_MAX && word(0)==0x7ffffffe);
    CHECK(!call("__errno",0,0,0));uint32_t err=cpu.r[0];CHECK(word(err-0x1000)==22);
    CHECK(!call("sem_init",0x1000,0,0) && word(0)==0);
    CHECK(call("sem_wait",0x1000,0,0)==-32); /* no success stub */
    strcpy((char*)mem+32,"/proc/cpuinfo");strcpy((char*)mem+64,"r");
    CHECK(!call("fopen",0x1020,0x1040,0) && !cpu.r[0] && word(err-0x1000)==2);
    strcpy((char*)mem+64,"w");CHECK(call("fopen",0x1020,0x1040,0)==-32);
    strcpy((char*)mem+32,"asset.bin");strcpy((char*)mem+64,"r");
    CHECK(call("fopen",0x1020,0x1040,0)==-32);
    CHECK(call("fopen",0x2000,0x1040,0)==-31);
    now=123456789;CHECK(!call("time",0,0,0) && cpu.r[0]==123456789);
    CHECK(!call("time",0x1000,0,0) && word(0)==123456789);
    CHECK(call("time",0x1fff,0,0)==-31);
    now=(time_t)-1;CHECK(!call("time",0x1000,0,0) && word(0)==UINT32_MAX);
    now=(time_t)INT32_MAX+1;CHECK(call("time",0x1000,0,0)==-32 && word(0)==UINT32_MAX);
    puts("startup guards, atomic swap, semaphore init, procfs absence and time ABI passed");
    return 0;
}
