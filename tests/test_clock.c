#include "../src/runtime/imports.h"
#include "../src/cpu/armv6.h"
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
static clock_t ticks;
clock_t clock(void) { return ticks; }
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x); exit(1); } } while (0)
static void check(clock_t input, uint32_t expected) {
    ticks=input;
    DsArmv6Cpu cpu={0};cpu.r[1]=42;cpu.r[14]=0x2001;
    CHECK(ds_import_call((uint32_t)(uintptr_t)ds_import_resolve(NULL,"clock"),&cpu)==0);
    CHECK(cpu.r[0]==expected && cpu.r[1]==42 && cpu.pc==0x2000 && cpu.thumb);
}
int main(void) {
    ds_imports_init();
    check(0,0);check(CLOCKS_PER_SEC,1000000);
    check(CLOCKS_PER_SEC/2,500000);
    check((clock_t)-1,UINT32_MAX);
    check((clock_t)2148*CLOCKS_PER_SEC,UINT32_MAX);
    puts("clock microsecond conversion, error, overflow and return ABI passed");
    return 0;
}
