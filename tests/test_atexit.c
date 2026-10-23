#include "../src/runtime/imports.h"
#include "../src/cpu/armv6.h"
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x); exit(1); } } while (0)
static uint32_t register_exit(const char *name,uint32_t r0,uint32_t r1,uint32_t dso) {
    DsArmv6Cpu cpu={0};cpu.r[0]=r0;cpu.r[1]=r1;cpu.r[2]=dso;cpu.r[14]=0x2001;
    CHECK(ds_import_call((uint32_t)(uintptr_t)ds_import_resolve(NULL,name),&cpu)==0);
    CHECK(cpu.pc==0x2000 && cpu.thumb);return cpu.r[0];
}
int main(void) {
    DsGuestDestructor d={0};ds_imports_init();
    CHECK(ds_import_take_destructor(0,NULL)==-1);
    CHECK(ds_import_take_destructor(0,&d)==0);
    CHECK(register_exit("__aeabi_atexit",0x1110,0x1001,7)==0);
    CHECK(register_exit("__cxa_atexit",0x2000,0x2220,8)==0);
    CHECK(register_exit("__aeabi_atexit",0x3330,0x3000,7)==0);
    CHECK(ds_import_take_destructor(7,&d)==1 && d.function==0x3000 && d.argument==0x3330 && d.dso==7);
    CHECK(ds_import_take_destructor(7,&d)==1 && d.function==0x1001 && d.argument==0x1110);
    CHECK(ds_import_take_destructor(7,&d)==0);
    CHECK(ds_import_take_destructor(0,&d)==1 && d.function==0x2000 && d.argument==0x2220 && d.dso==8);
    CHECK(ds_import_take_destructor(0,&d)==0); /* exactly once */
    CHECK(register_exit("__cxa_atexit",0,1,7)==UINT32_MAX);
    for(unsigned i=0;i<1024;++i)CHECK(register_exit("__aeabi_atexit",i,0x1001,9)==0);
    CHECK(register_exit("__aeabi_atexit",42,0x1001,9)==UINT32_MAX);
    for(unsigned i=1024;i>0;--i)CHECK(ds_import_take_destructor(0,&d)==1 && d.argument==i-1);
    CHECK(register_exit("__aeabi_atexit",42,0x1001,9)==0);
    ds_imports_init();CHECK(ds_import_take_destructor(0,&d)==0);
    DsArmImage image={0};CHECK(register_exit("__aeabi_atexit",42,0x1001,9)==0);
    ds_imports_bind_image(&image);CHECK(ds_import_take_destructor(0,&d)==0);
    DsArmv6Cpu cpu={0};cpu.pc=0x4000;
    CHECK(ds_import_call((uint32_t)(uintptr_t)ds_import_resolve(NULL,"__cxa_finalize"),&cpu)==-32 && cpu.pc==0x4000);
    puts("ARM/C++ registration ABI, reverse ordering, DSO filters and capacity passed");return 0;
}
