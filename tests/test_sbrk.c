#include "../src/runtime/imports.h"
#include "../src/cpu/armv6.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x); exit(1); } } while (0)
static unsigned char mem[4096];
static DsArmv6Cpu cpu;
static uint32_t call(const char *name,uint32_t arg) {
    cpu.r[0]=arg; cpu.r[14]=0x2001;
    CHECK(ds_import_call((uint32_t)(uintptr_t)ds_import_resolve(NULL,name),&cpu)==0);
    CHECK(cpu.pc==0x2000 && cpu.thumb); return cpu.r[0];
}
int main(void) {
    DsArmImage im={.image=mem,.image_base=0x1000,.image_size=1024,
        .heap_base=0x1400,.heap_size=3072,.bridge_size=1024,
        .brk_base=0x1800,.brk_size=1024,.stack_base=0x1c00,.stack_size=1024};
    memset(mem,0xa5,sizeof(mem));
    ds_imports_init(); ds_imports_bind_image(&im);
    uint32_t err=call("__errno",0); CHECK(err==0x1400 && mem[1024]==0);
    CHECK(call("sbrk",0)==0x1800);
    CHECK(call("sbrk",3)==0x1800 && im.brk_used==3);
    CHECK(call("malloc",8)==0x1408); /* separate even with an unaligned break */
    CHECK(call("sbrk",1021)==0x1803 && im.brk_used==1024);
    CHECK(call("sbrk",1)==UINT32_MAX && im.brk_used==1024 && mem[1024]==12);
    CHECK(call("sbrk",0)==0x1c00 && mem[1024]==12); /* success preserves errno */
    CHECK(call("sbrk",UINT32_MAX)==0x1c00 && im.brk_used==1023);
    CHECK(call("sbrk",(uint32_t)-1023)==0x1bff && im.brk_used==0);
    CHECK(call("sbrk",UINT32_MAX)==UINT32_MAX && im.brk_used==0);
    CHECK(call("sbrk",0x80000000)==UINT32_MAX && im.brk_used==0);
    CHECK(call("sbrk",0x7fffffff)==UINT32_MAX && im.brk_used==0);
    CHECK(call("malloc",1008)==0x1410);
    CHECK(call("malloc",1)==0 && call("sbrk",1024)==0x1800);
    CHECK(call("__errno",0)==err);
    for(unsigned i=2048;i<4096;++i) CHECK(mem[i]==0xa5); /* no break/stack writes */
    puts("program break bounds, signed increments, errno and pool isolation passed"); return 0;
}
