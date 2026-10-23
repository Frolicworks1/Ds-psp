#include "../src/runtime/imports.h"
#include "../src/cpu/armv6.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x); exit(1); } } while (0)
static unsigned char mem[24576];
static DsArmv6Cpu cpu;
static uint32_t word(unsigned i) { return (uint32_t)mem[i]|(uint32_t)mem[i+1]<<8|(uint32_t)mem[i+2]<<16|(uint32_t)mem[i+3]<<24; }
static void put(unsigned i,uint32_t v) { for(unsigned b=0;b<4;++b)mem[i+b]=(unsigned char)(v>>(8*b)); }
static int call(const char *name,uint32_t addr,uint32_t size) {
    cpu.r[0]=addr;cpu.r[1]=size;cpu.r[14]=0x8001;cpu.pc=0x1234;
    int rc=ds_import_call((uint32_t)(uintptr_t)ds_import_resolve(NULL,name),&cpu);
    if(rc) CHECK(cpu.pc==0x1234 && cpu.r[0]==addr);
    else CHECK(cpu.pc==0x8000 && cpu.thumb);
    return rc;
}
#define RESULT(name,a,b,result) do { CHECK(call(name,a,b)==0);CHECK(cpu.r[0]==result); } while(0)
int main(void) {
    DsArmImage im={.image=mem,.image_base=0x1000,.image_size=4096,.heap_base=0x2000,
        .heap_size=20480,.bridge_size=4096,.mmap_base=0x3000,.mmap_size=12288,
        .stack_base=0x6000,.stack_size=4096};
    memset(mem,0xa5,sizeof(mem));ds_imports_init();ds_imports_bind_image(&im);
    cpu.r[13]=0x6000;put(0x5000,UINT32_MAX);put(0x5004,0);
    cpu.r[2]=3;cpu.r[3]=0x22;
    RESULT("mmap",0,4097,0x3000);
    for(unsigned i=0x2000;i<0x4000;++i)CHECK(mem[i]==0);
    RESULT("mmap",0xdead0000,1,0x5000); /* ignored non-fixed hint */
    RESULT("mmap",0,1,UINT32_MAX);CHECK(word(0x1000)==12);
    mem[0x3000]=42; RESULT("munmap",0x4000,1,0); /* partial page release */
    RESULT("mmap",0,4096,0x4000);CHECK(mem[0x3000]==0 && word(0x1000)==12);
    RESULT("munmap",0x3000,4096,0);
    RESULT("munmap",0x5000,4096,0);
    RESULT("mmap",0,8192,UINT32_MAX); /* free pages are not contiguous */
    RESULT("munmap",0x3000,12288,0);
    RESULT("munmap",0x3000,12288,0); /* unmapped holes are permitted */
    RESULT("mmap",0,12288,0x3000);
    RESULT("mmap",0,0,UINT32_MAX);CHECK(word(0x1000)==22);
    RESULT("mmap",0,UINT32_MAX,UINT32_MAX);CHECK(word(0x1000)==12);
    RESULT("munmap",0x3001,1,UINT32_MAX);CHECK(word(0x1000)==22);
    CHECK(call("munmap",0x6000,4096)==-32);
    CHECK(call("munmap",0xfffff000,8192)==-32);
    cpu.r[3]=0x32;CHECK(call("mmap",0x3000,4096)==-32);cpu.r[3]=0x22;
    cpu.r[2]=1;CHECK(call("mmap",0,4096)==-32);cpu.r[2]=3;
    put(0x5000,3);CHECK(call("mmap",0,4096)==-32);put(0x5000,UINT32_MAX);
    put(0x5004,4096);CHECK(call("mmap",0,4096)==-32);put(0x5004,0);
    cpu.r[13]=0x6ffc;CHECK(call("mmap",0,4096)==-31);cpu.r[13]=0x6000;
    RESULT("munmap",0x3000,12288,0);
    for(unsigned i=0;i<4096;++i)CHECK(mem[i]==0xa5);
    for(unsigned i=0x5008;i<sizeof(mem);++i)CHECK(mem[i]==0xa5);
    ds_imports_init();ds_imports_bind_image(&im);
    RESULT("mmap",0,12288,0x3000); /* ownership reset for fresh binding */
    puts("mapping allocation, fragmentation, zero fill, errors and ARM stack ABI passed");return 0;
}
