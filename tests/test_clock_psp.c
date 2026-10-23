#include "../src/runtime/imports.h"
#include "../src/cpu/armv6.h"
#include "psp_clock/pspthreadman.h"
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x); exit(1); } } while (0)
static uint64_t runtime;
static int status_error,conversion_error;
int sceKernelGetThreadId(void) {return 7;}
int sceKernelReferThreadRunStatus(int id,SceKernelThreadRunStatus *s) {
    CHECK(id==7 && s->size==sizeof(*s));
    s->runClocks.low=(uint32_t)runtime;s->runClocks.hi=(uint32_t)(runtime>>32);
    return status_error;
}
int sceKernelSysClock2USec(SceKernelSysClock *s,unsigned *seconds,unsigned *microseconds) {
    uint64_t us=((uint64_t)s->hi<<32)|s->low;
    *seconds=(unsigned)(us/1000000);*microseconds=(unsigned)(us%1000000);
    return conversion_error;
}
static void check(uint64_t input,uint32_t expected) {
    runtime=input;DsArmv6Cpu cpu={0};cpu.r[14]=0x2001;
    CHECK(!ds_import_call((uint32_t)(uintptr_t)ds_import_resolve(NULL,"clock"),&cpu));
    CHECK(cpu.r[0]==expected && cpu.pc==0x2000 && cpu.thumb);
}
int main(void) {
    ds_imports_init();check(0,0);check(1234567,1234567);check(INT32_MAX,INT32_MAX);
    check((uint64_t)INT32_MAX+1,UINT32_MAX);check(UINT64_C(0x123456789),UINT32_MAX);
    status_error=-1;check(100,UINT32_MAX);status_error=0;
    conversion_error=-1;check(100,UINT32_MAX);
    puts("PSP clock adapter units, range and kernel failure paths passed (mock kernel)");return 0;
}
