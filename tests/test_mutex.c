#include "../src/runtime/imports.h"
#include "../src/cpu/armv6.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x); exit(1); } } while (0)
static unsigned char mem[64];
static DsArmv6Cpu cpu;
static uint32_t word(unsigned i) {
    return (uint32_t)mem[i] | (uint32_t)mem[i+1]<<8 | (uint32_t)mem[i+2]<<16 | (uint32_t)mem[i+3]<<24;
}
static void put(unsigned i,uint32_t v) { for(unsigned b=0;b<4;++b) mem[i+b]=(unsigned char)(v>>(8*b)); }
static int call(const char *name,uint32_t a,uint32_t b) {
    cpu.r[0]=a; cpu.r[1]=b; cpu.r[14]=0x2001; cpu.pc=0x1234;
    int rc=ds_import_call((uint32_t)(uintptr_t)ds_import_resolve(NULL,name),&cpu);
    if (rc) CHECK(cpu.pc==0x1234 && cpu.r[0]==a);
    else CHECK(cpu.pc==0x2000 && cpu.thumb);
    return rc;
}
#define RESULT(name,a,b,result) do { CHECK(call(name,a,b)==0); CHECK(cpu.r[0]==result); } while(0)
int main(void) {
    DsArmImage image={.image=mem,.image_base=0x1000,.image_size=sizeof(mem)};
    ds_imports_init(); ds_imports_bind_image(&image);
    RESULT("pthread_mutexattr_init",0x1000,0,0); CHECK(word(0)==0);
    for (unsigned type=0;type<3;++type) {
        RESULT("pthread_mutexattr_settype",0x1000,type,0);
        RESULT("pthread_mutexattr_gettype",0x1000,0x1008,0); CHECK(word(8)==type);
        RESULT("pthread_mutex_init",0x1004,0x1000,0); CHECK(word(4)==type<<14);
        RESULT("pthread_mutex_lock",0x1004,0,0);
        CHECK(word(4)==((type<<14)|1u|(type?0x10000u:0)));
        if (type==0) {
            RESULT("pthread_mutex_trylock",0x1004,0,16);
            CHECK(call("pthread_mutex_lock",0x1004,0)==-32);
        } else if (type==1) {
            RESULT("pthread_mutex_lock",0x1004,0,0); CHECK(word(4)==0x14005);
            RESULT("pthread_mutex_unlock",0x1004,0,0); CHECK(word(4)==0x14001);
        } else {
            RESULT("pthread_mutex_lock",0x1004,0,35);
            RESULT("pthread_mutex_trylock",0x1004,0,35);
        }
        RESULT("pthread_mutex_destroy",0x1004,0,16);
        RESULT("pthread_mutex_unlock",0x1004,0,0); CHECK(word(4)==type<<14);
        if(type) RESULT("pthread_mutex_unlock",0x1004,0,1);
        RESULT("pthread_mutex_destroy",0x1004,0,0); CHECK(word(4)==0xdead10cc);
        RESULT("pthread_mutex_lock",0x1004,0,22);
    }
    RESULT("pthread_mutex_init",0x1004,0,0); CHECK(word(4)==0);
    put(4,0x24001); RESULT("pthread_mutex_trylock",0x1004,0,16);
    CHECK(call("pthread_mutex_lock",0x1004,0)==-32);
    RESULT("pthread_mutex_unlock",0x1004,0,1);
    put(4,0x15ffd); RESULT("pthread_mutex_lock",0x1004,0,11); CHECK(word(4)==0x15ffd);
    RESULT("pthread_mutexattr_setpshared",0x1000,1,0);
    RESULT("pthread_mutexattr_getpshared",0x1000,0x1008,0); CHECK(word(8)==1);
    CHECK(call("pthread_mutex_init",0x1004,0x1000)==-32);
    RESULT("pthread_mutexattr_setpshared",0x1000,0,0);
    RESULT("pthread_mutexattr_settype",0x1000,3,22);
    RESULT("pthread_mutexattr_setpshared",0x1000,2,22);
    RESULT("pthread_mutexattr_destroy",0x1000,0,0); CHECK(word(0)==UINT32_MAX);
    RESULT("pthread_mutex_init",0x1004,0x1000,22);
    RESULT("pthread_mutex_init",0,0,22);
    CHECK(call("pthread_mutex_lock",0x1040,0)==-31);
    CHECK(call("pthread_mutex_lock",0x1001,0)==-31);
    CHECK(call("pthread_mutex_init",0x1004,0x103f)==-31);
    CHECK(call("pthread_mutexattr_gettype",0x1000,0x1040)==-31);
    put(4,0x14002); CHECK(call("pthread_mutex_unlock",0x1004,0)==-32);
    put(4,0x10001); RESULT("pthread_mutex_lock",0x1004,0,22);
    put(4,0x4001); RESULT("pthread_mutex_lock",0x1004,0,22);
    CHECK(call("pthread_mutex_timedlock",0,0)==-32);
    CHECK(call("pthread_create",0x1000,0)==-32);
    puts("mutex attributes, state transitions and unsupported concurrency passed"); return 0;
}
