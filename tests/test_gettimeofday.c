#include "../src/runtime/imports.h"
#include "../src/cpu/armv6.h"
#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static unsigned char mem[4096];
static struct timeval sample;
static unsigned calls;
static int failure;
int gettimeofday(struct timeval *out,void *zone) {
 CHECK(!zone);++calls;*out=sample;return failure;
}
static int call(uint32_t out,uint32_t zone) {
 DsArmv6Cpu cpu={0};cpu.r[0]=out;cpu.r[1]=zone;cpu.r[2]=0x12345678;cpu.r[14]=0x4001;cpu.pc=0x3000;
 int rc=ds_import_call((uint32_t)(uintptr_t)ds_import_resolve(NULL,"gettimeofday"),&cpu);
 CHECK(cpu.r[1]==zone && cpu.r[2]==0x12345678);
 CHECK(rc?cpu.pc==0x3000:cpu.pc==0x4000 && cpu.thumb && !cpu.r[0]);return rc;
}
static uint32_t word(unsigned off){uint32_t v;memcpy(&v,mem+off,4);return v;}
int main(void) {
 DsArmImage image={.image=mem,.image_base=0x1000,.image_size=2048,.heap_base=0x1800,.heap_size=2048};
 ds_imports_init();ds_imports_bind_image(&image);
 sample.tv_sec=1700000000;sample.tv_usec=987654;
 memset(mem,0xaa,sizeof(mem));CHECK(!call(0x1004,0));
 CHECK(word(4)==1700000000 && word(8)==987654 && word(0)==0xaaaaaaaa && word(12)==0xaaaaaaaa);
 sample.tv_sec=INT32_MIN;sample.tv_usec=0;CHECK(!call(0x1004,0) && word(4)==0x80000000);
 sample.tv_sec=INT32_MAX;sample.tv_usec=999999;CHECK(!call(0x1004,0) && word(4)==INT32_MAX);
 unsigned count=calls;
 CHECK(call(0x1ffc,0)==-31 && call(0x1001,0)==-31 && call(0x0ffc,0)==-31);
 CHECK(call(0x1004,0x1010)==-32 && !call(0,0) && calls==count);
 sample.tv_sec=(time_t)INT32_MAX+1;CHECK(call(0x1004,0)==-32 && word(4)==INT32_MAX);
 sample.tv_sec=(time_t)INT32_MIN-1;CHECK(call(0x1004,0)==-32 && word(4)==INT32_MAX);
 sample.tv_sec=100;sample.tv_usec=-1;CHECK(call(0x1004,0)==-32 && word(4)==INT32_MAX);
 sample.tv_usec=1000000;CHECK(call(0x1004,0)==-32 && word(4)==INT32_MAX);
 sample.tv_usec=20;failure=-1;CHECK(call(0x1004,0)==-32 && word(4)==INT32_MAX);
 puts("gettimeofday ARM32 layout, wall time, bounds, overflow and failure preservation passed");
}
