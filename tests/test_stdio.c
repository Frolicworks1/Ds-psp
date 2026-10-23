#include "../src/runtime/imports.h"
#include "../src/cpu/armv6.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x); exit(1); } } while (0)
static unsigned char arena[4096], output[128];
static unsigned output_size;
static int sink_limit=128;
void dspsp_log(const char *fmt, ...) { (void)fmt; }
int dspsp_log_write(const void *p, unsigned size) {
    if (sink_limit<0) return -1;
    if (size>(unsigned)sink_limit) size=(unsigned)sink_limit;
    CHECK(output_size+size<=sizeof(output));
    memcpy(output+output_size,p,size); output_size+=size; return (int)size;
}
static uint32_t word(unsigned off) {
    return (uint32_t)arena[off] | (uint32_t)arena[off+1]<<8 |
        (uint32_t)arena[off+2]<<16 | (uint32_t)arena[off+3]<<24;
}
static int call(const char *name, DsArmv6Cpu *cpu) {
    return ds_import_call((uint32_t)(uintptr_t)ds_import_resolve(NULL,name),cpu);
}
int main(void) {
    DsArmImage im={.image=arena,.image_base=0x1000,.image_size=256,.heap_base=0x1100,.heap_size=3840};
    ds_imports_init(); ds_imports_bind_image(&im);
    uint32_t streams=ds_import_resolve_data("__sF"); CHECK(streams==0x1100);
    uint32_t used=im.heap_used; CHECK(ds_import_resolve_data("__sF")==streams && im.heap_used==used);
    /* Independent offsets derived from the ARM header: sizeof(FILE)=84,
       cookie=28, callbacks=32/36/40/44, extension=48; extensions are 12 bytes. */
    for (unsigned i=0;i<3;++i) {
        unsigned off=256+i*84;
        CHECK(arena[off+12]==(i==0?4:i==1?8:10)); CHECK(arena[off+14]==i);
        CHECK(word(off+28)==streams+i*84);
        CHECK(word(off+48)==streams+252+i*12);
        CHECK(word(off)==0 && word(off+4)==0 && word(off+8)==0);
        for (unsigned j=0;j<4;++j) CHECK(ds_import_is_trampoline(word(off+32+j*4)));
        for (unsigned j=0;j<12;++j) CHECK(arena[256+252+i*12+j]==0);
    }
    memcpy(arena,"A\0BC\n",5);
    DsArmv6Cpu cpu={0}; cpu.r[14]=0x2001;
    cpu.r[0]=0x1000; cpu.r[1]=1; cpu.r[2]=5; cpu.r[3]=streams+84;
    CHECK(call("fwrite",&cpu)==0 && cpu.r[0]==5 && cpu.pc==0x2000 && cpu.thumb);
    CHECK(output_size==5 && !memcmp(output,"A\0BC\n",5));
    sink_limit=3; cpu.r[0]=0x1000; cpu.r[1]=2; cpu.r[2]=2; cpu.r[3]=streams+168;
    CHECK(call("fwrite",&cpu)==0 && cpu.r[0]==1);
    cpu.r[0]=streams+168; CHECK(call("ferror",&cpu)==0 && cpu.r[0]==1);
    cpu.r[0]=streams+168; CHECK(call("clearerr",&cpu)==0);
    cpu.r[0]=streams+168; CHECK(call("ferror",&cpu)==0 && cpu.r[0]==0);
    sink_limit=-1; cpu.r[0]=0x1000; cpu.r[1]=streams+84;
    CHECK(call("fputs",&cpu)==0 && cpu.r[0]==UINT32_MAX);
    sink_limit=0; cpu.r[0]=0x1000; cpu.r[1]=streams+84;
    CHECK(call("fputs",&cpu)==0 && cpu.r[0]==UINT32_MAX);
    sink_limit=128; cpu.r[0]=0x1000; cpu.r[1]=streams+84;
    CHECK(call("fputs",&cpu)==0 && cpu.r[0]==0);
    cpu.r[0]=streams+168; CHECK(call("fileno",&cpu)==0 && cpu.r[0]==2);
    cpu.r[0]=streams+84; CHECK(call("fflush",&cpu)==0 && cpu.r[0]==0);
    cpu.r[0]=streams+1; CHECK(call("fflush",&cpu)==-32);
    cpu.r[0]=0x1000; cpu.r[1]=1; cpu.r[2]=1; cpu.r[3]=streams;
    CHECK(call("fwrite",&cpu)==-32); /* input stream is not an output sink */
    cpu.r[0]=0x1fff; cpu.r[1]=1; cpu.r[2]=2; cpu.r[3]=streams+84;
    CHECK(call("fwrite",&cpu)==-31);
    cpu.r[0]=0x1000; cpu.r[1]=UINT32_MAX; cpu.r[2]=2;
    CHECK(call("fwrite",&cpu)==-31);
    cpu.r[0]=streams+84; cpu.r[1]=0x1000; cpu.r[2]=5;
    CHECK(ds_import_call(word(256+84+44),&cpu)==0 && cpu.r[0]==5);
    cpu.r[0]=streams; CHECK(ds_import_call(word(256+36),&cpu)==-32);
    uint32_t page=ds_import_resolve_data("__page_size"); CHECK(word(page-0x1000)==4096);
    uint32_t guard=ds_import_resolve_data("__stack_chk_guard"); CHECK(word(guard-0x1000)==0xff0a0000u);
    uint32_t tz=ds_import_resolve_data("tzname");
    CHECK(word(tz-0x1000)==tz+8 && word(tz-0x1000+4)==tz+8);
    CHECK(!memcmp(arena+tz-0x1000+8,"   ",4));
    used=im.heap_used;
    CHECK(ds_import_resolve_data("__page_size")==page && ds_import_resolve_data("tzname")==tz && im.heap_used==used);
    cpu.pc=0x1234; cpu.r[0]=42; CHECK(call("__stack_chk_fail",&cpu)==-33 && cpu.pc==0x1234 && cpu.r[0]==42);
    /* PLT-style LDR pc must dispatch on the following step, before fetch. */
    uint32_t target=(uint32_t)(uintptr_t)ds_import_resolve(0,"fileno");
    arena[0]=4; arena[1]=0xf0; arena[2]=0x1f; arena[3]=0xe5; /* ldr pc,[pc,#-4] */
    for (unsigned b=0;b<4;++b) arena[4+b]=(unsigned char)(target>>(8*b));
    ds_armv6_reset(&cpu,0x1000); cpu.r[0]=streams+84; cpu.r[14]=0x2001;
    DsArmv6Image image={arena,sizeof(arena),0x1000};
    CHECK(ds_armv6_step(&cpu,&image)==0 && cpu.pc==target);
    CHECK(ds_armv6_step(&cpu,&image)==0 && cpu.r[0]==1 && cpu.pc==0x2000 && cpu.thumb);
    cpu.pc=(uint32_t)(uintptr_t)ds_import_resolve(0,"unsupported_test"); cpu.thumb=0;
    CHECK(ds_armv6_step(&cpu,&image)==-30);
    /* Failed storage allocation must not leak callback registrations. */
    ds_imports_init(); ds_imports_bind_image(&im); im.heap_used=0; im.heap_size=8;
    CHECK(ds_import_resolve_data("__sF")==0 && im.heap_used==0 && ds_import_resolve_count()==0);
    puts("standard stream layout, byte output and error checks passed"); return 0;
}
