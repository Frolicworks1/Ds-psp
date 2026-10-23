#include "../src/cpu/armv6.h"
#include "../src/runtime/imports.h"
#include <stdio.h>
#include <string.h>

int ds_import_is_trampoline(uint32_t addr) { (void)addr; return 0; }
int ds_import_call(uint32_t addr, struct DsArmv6Cpu *cpu) {
    (void)addr; (void)cpu; return -1;
}

#define N 0x80000000u
#define Z 0x40000000u
#define C 0x20000000u
#define V 0x10000000u
#define FLAGS (N|Z|C|V)
#define CHECK(expr) do { if (!(expr)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr); return 1; } } while (0)

typedef struct {
    DsArmv6Cpu cpu;
    uint32_t words[64];
    DsArmv6Image image;
} Fixture;

static void reset(Fixture *f, uint32_t insn, int thumb) {
    memset(f, 0, sizeof(*f));
    f->words[0] = insn;
    f->image = (DsArmv6Image){(uint8_t *)f->words, sizeof(f->words), 0x1000u};
    ds_armv6_reset(&f->cpu, 0x1000u | (unsigned)thumb);
}

static int selftest(void) { CHECK(ds_armv6_selftest() == 0); return 0; }

static int fetch_bounds(void) {
    Fixture f; reset(&f, 0xe1a00000u, 0);
    CHECK(ds_armv6_step(NULL, &f.image) == -1);
    CHECK(ds_armv6_step(&f.cpu, NULL) == -1);
    f.image.base = NULL;
    CHECK(ds_armv6_step(&f.cpu, &f.image) == -1);
    reset(&f, 0xe1a00000u, 0); f.image.size = 3;
    CHECK(ds_armv6_step(&f.cpu, &f.image) == -1);
    CHECK(f.cpu.pc == 0x1000u && f.cpu.steps == 0);
    reset(&f, 0x2000u, 1); f.image.size = 1;
    CHECK(ds_armv6_step(&f.cpu, &f.image) == -1);
    return 0;
}

static int flags_only(void) {
    Fixture f;
    const uint32_t insns[] = {0xe3500003u, 0xe3100003u, 0xe3300003u, 0xe3700003u};
    const uint32_t values[] = {3, 4, 3, 0xfffffffdu};
    const uint32_t flags[] = {Z|C, Z|C|V, Z|C|V, Z|C};
    for (unsigned i=0; i<4; ++i) {
        reset(&f, insns[i], 0); f.cpu.r[0]=values[i]; f.cpu.cpsr=C|V;
        CHECK(ds_armv6_step(&f.cpu,&f.image)==0);
        CHECK(f.cpu.r[0]==values[i]);
        CHECK((f.cpu.cpsr&FLAGS)==flags[i]);
        CHECK(f.cpu.pc==0x1004u);
    }
    return 0;
}

static int arithmetic_flags(void) {
    const struct { uint32_t insn,a,b,result,flags; } vectors[] = {
        {0xe0910002u,0xffffffffu,1,0,Z|C},
        {0xe0910002u,0x7fffffffu,1,0x80000000u,N|V},
        {0xe0910002u,0x80000000u,0x80000000u,0,Z|C|V},
        {0xe0510002u,0,1,0xffffffffu,N},
        {0xe0510002u,7,7,0,Z|C},
        {0xe0510002u,0x80000000u,1,0x7fffffffu,C|V},
        {0xe0710002u,1,0,0xffffffffu,N}
    };
    for (unsigned i=0;i<sizeof(vectors)/sizeof(vectors[0]);++i) {
        Fixture f; reset(&f,vectors[i].insn,0);
        f.cpu.r[1]=vectors[i].a; f.cpu.r[2]=vectors[i].b; f.cpu.cpsr=FLAGS;
        CHECK(ds_armv6_step(&f.cpu,&f.image)==0);
        CHECK(f.cpu.r[0]==vectors[i].result);
        CHECK((f.cpu.cpsr&FLAGS)==vectors[i].flags);
    }
    return 0;
}

static int carry_arithmetic(void) {
    Fixture f; reset(&f,0xe0b10002u,0); /* ADCS r0,r1,r2 */
    f.cpu.r[1]=0xffffffffu; f.cpu.cpsr=C;
    CHECK(ds_armv6_step(&f.cpu,&f.image)==0);
    CHECK(f.cpu.r[0]==0 && (f.cpu.cpsr&FLAGS)==(Z|C));
    reset(&f,0xe0d10002u,0); /* SBCS, borrow when input C is clear */
    f.cpu.r[1]=0; f.cpu.r[2]=0;
    CHECK(ds_armv6_step(&f.cpu,&f.image)==0);
    CHECK(f.cpu.r[0]==0xffffffffu && (f.cpu.cpsr&FLAGS)==N);
    return 0;
}

static int register_shifts(void) {
    const struct { unsigned type,amount; uint32_t result,carry; } vectors[] = {
        {0,0,0x80000001u,C},{0,1,2,C},{0,32,0,C},{0,33,0,0},{0,255,0,0},
        {1,0,0x80000001u,C},{1,1,0x40000000u,C},{1,32,0,C},{1,33,0,0},{1,255,0,0},
        {2,0,0x80000001u,C},{2,1,0xc0000000u,C},{2,32,0xffffffffu,C},{2,33,0xffffffffu,C},{2,255,0xffffffffu,C},
        {3,0,0x80000001u,C},{3,1,0xc0000000u,C},{3,32,0x80000001u,C},{3,33,0xc0000000u,C},{3,255,3,0}
    };
    for (unsigned i=0;i<sizeof(vectors)/sizeof(vectors[0]);++i) {
        Fixture f; reset(&f,0xe1b00211u|(vectors[i].type<<5),0); /* MOVS r0,r1,<shift>r2 */
        f.cpu.r[1]=0x80000001u; f.cpu.r[2]=vectors[i].amount; f.cpu.cpsr=C|V;
        CHECK(ds_armv6_step(&f.cpu,&f.image)==0);
        CHECK(f.cpu.r[0]==vectors[i].result);
        CHECK((f.cpu.cpsr&C)==vectors[i].carry && (f.cpu.cpsr&V)==V);
    }
    return 0;
}

static int rrx(void) {
    Fixture f; reset(&f,0xe1b00061u,0);
    f.cpu.r[1]=0x80000003u; f.cpu.cpsr=V;
    CHECK(ds_armv6_step(&f.cpu,&f.image)==0);
    CHECK(f.cpu.r[0]==0x40000001u && (f.cpu.cpsr&FLAGS)==(C|V));
    return 0;
}

static int arm_branches(void) {
    Fixture f; reset(&f,0xeafffffeu,0); /* B . */
    CHECK(ds_armv6_step(&f.cpu,&f.image)==0 && f.cpu.pc==0x1000u);
    reset(&f,0xeb000000u,0);
    CHECK(ds_armv6_step(&f.cpu,&f.image)==0 && f.cpu.pc==0x1008u && f.cpu.r[14]==0x1004u);
    reset(&f,0x1a000000u,0); f.cpu.cpsr=Z;
    CHECK(ds_armv6_step(&f.cpu,&f.image)==0 && f.cpu.pc==0x1004u);
    return 0;
}

static int blx_immediate(void) {
    for (unsigned h=0; h<2; ++h) {
        Fixture f; reset(&f,0xfa000000u|(h<<24),0);
        CHECK(ds_armv6_step(&f.cpu,&f.image)==0);
        CHECK(f.cpu.thumb==1 && f.cpu.pc==0x1008u+2*h && f.cpu.r[14]==0x1004u);
    }
    return 0;
}

static int thumb_immediates(void) {
    Fixture f; reset(&f,0x2803u,1); f.cpu.r[0]=3; /* CMP */
    CHECK(ds_armv6_step(&f.cpu,&f.image)==0);
    CHECK(f.cpu.r[0]==3 && (f.cpu.cpsr&FLAGS)==(Z|C));
    reset(&f,0x3001u,1); f.cpu.r[0]=0x7fffffffu; /* ADDS */
    CHECK(ds_armv6_step(&f.cpu,&f.image)==0);
    CHECK(f.cpu.r[0]==0x80000000u && (f.cpu.cpsr&FLAGS)==(N|V));
    reset(&f,0x3801u,1); /* SUBS */
    CHECK(ds_armv6_step(&f.cpu,&f.image)==0);
    CHECK(f.cpu.r[0]==0xffffffffu && (f.cpu.cpsr&FLAGS)==N);
    reset(&f,0x2007u,1); f.cpu.cpsr=C|V; /* MOVS preserves C/V */
    CHECK(ds_armv6_step(&f.cpu,&f.image)==0);
    CHECK(f.cpu.r[0]==7 && (f.cpu.cpsr&FLAGS)==(C|V));
    return 0;
}

static int thumb_branches(void) {
    Fixture f; reset(&f,0xd000u,1); f.cpu.cpsr=Z;
    CHECK(ds_armv6_step(&f.cpu,&f.image)==0 && f.cpu.pc==0x1004u);
    reset(&f,0xd0ffu,1); f.cpu.cpsr=Z;
    CHECK(ds_armv6_step(&f.cpu,&f.image)==0 && f.cpu.pc==0x1002u);
    reset(&f,0xe7feu,1);
    CHECK(ds_armv6_step(&f.cpu,&f.image)==0 && f.cpu.pc==0x1000u);
    reset(&f,0x4788u,1); f.cpu.r[1]=0x2000u; /* BLX r1 */
    CHECK(ds_armv6_step(&f.cpu,&f.image)==0);
    CHECK(f.cpu.pc==0x2000u && f.cpu.thumb==0 && f.cpu.r[14]==0x1003u);
    reset(&f,0xde00u,1);
    CHECK(ds_armv6_step(&f.cpu,&f.image)==-20);
    return 0;
}

static int memory_bounds(void) {
    Fixture f; reset(&f,0xe5910000u,0); /* LDR r0,[r1] */
    f.words[63]=0x12345678u; f.cpu.r[1]=0x1000u+sizeof(f.words)-4;
    CHECK(ds_armv6_step(&f.cpu,&f.image)==0 && f.cpu.r[0]==0x12345678u);
    reset(&f,0xe5910000u,0); f.cpu.r[1]=0x1000u+sizeof(f.words)-3;
    CHECK(ds_armv6_step(&f.cpu,&f.image)==-3);
    reset(&f,0xe591f000u,0); /* LDR pc,[r1] switches to Thumb on ARMv5+ */
    f.words[63]=0x1001u; f.cpu.r[1]=0x1000u+sizeof(f.words)-4;
    CHECK(ds_armv6_step(&f.cpu,&f.image)==0 && f.cpu.pc==0x1000u && f.cpu.thumb==1);
    reset(&f,0xe7910062u,0); /* LDR r0,[r1,r2,RRX] with a nonzero Rm */
    f.words[63]=0x76543210u; f.cpu.r[1]=0x1000u+sizeof(f.words)-8; f.cpu.r[2]=8;
    CHECK(ds_armv6_step(&f.cpu,&f.image)==0 && f.cpu.r[0]==0x76543210u);
    reset(&f,0xe8b10005u,0); /* LDMIA r1!,{r0,r2} */
    f.cpu.r[1]=0x1000u+sizeof(f.words)-4;
    CHECK(ds_armv6_step(&f.cpu,&f.image)==-3);
    return 0;
}

static int halfword_transfers(void) {
    Fixture f; reset(&f,0xe1c021b4u,0); /* donor STRH r2,[r0,#20] */
    f.cpu.r[0]=0x1080; f.cpu.r[2]=0x1234abcd; f.words[37]=0xeeeeeeee;
    CHECK(ds_armv6_step(&f.cpu,&f.image)==0 && f.words[37]==0xeeeeabcd);
    reset(&f,0xe1d100b0u,0); f.cpu.r[1]=0x10fe; f.words[63]=0x80ff1234;
    CHECK(ds_armv6_step(&f.cpu,&f.image)==0 && f.cpu.r[0]==0x80ff);
    reset(&f,0xe1d100f0u,0); f.cpu.r[1]=0x10fe; f.words[63]=0x80ff1234;
    CHECK(ds_armv6_step(&f.cpu,&f.image)==0 && f.cpu.r[0]==0xffff80ff);
    reset(&f,0xe1d100d0u,0); f.cpu.r[1]=0x10ff; f.words[63]=0x80ff1234;
    CHECK(ds_armv6_step(&f.cpu,&f.image)==0 && f.cpu.r[0]==0xffffff80);
    reset(&f,0xe13100b2u,0); /* LDRH r0,[r1,-r2]! */
    f.cpu.r[1]=0x1100; f.cpu.r[2]=2; f.words[63]=0x7fff1234;
    CHECK(ds_armv6_step(&f.cpu,&f.image)==0 && f.cpu.r[0]==0x7fff && f.cpu.r[1]==0x10fe);
    reset(&f,0xe0c100b2u,0); /* STRH r0,[r1],#2 */
    f.cpu.r[1]=0x10fe; f.cpu.r[0]=0xabcd;
    CHECK(ds_armv6_step(&f.cpu,&f.image)==0 && f.cpu.r[1]==0x1100 && f.words[63]==0xabcd0000);
    reset(&f,0xe1df00b0u,0); f.words[2]=0x4321; /* PC-relative */
    CHECK(ds_armv6_step(&f.cpu,&f.image)==0 && f.cpu.r[0]==0x4321);
    const uint32_t invalid[]={0xe1f110b2,0xe1d1f0b0,0xe0f100b0,0xe19101b2,0xe19100bf};
    for(unsigned i=0;i<sizeof(invalid)/sizeof(*invalid);++i) {
        reset(&f,invalid[i],0); f.cpu.r[1]=0x1080;
        CHECK(ds_armv6_step(&f.cpu,&f.image)==-2 && f.cpu.pc==0x1000 && f.cpu.steps==0);
    }
    const uint32_t bad_addr[]={0x0ffe,0x10ff,0x1100,0xfffffffe};
    for(unsigned i=0;i<sizeof(bad_addr)/sizeof(*bad_addr);++i) {
        reset(&f,0xe0c100b2u,0); f.cpu.r[1]=bad_addr[i]; f.cpu.r[0]=0xabcd;
        uint32_t saved[64]; memcpy(saved,f.words,sizeof(saved));
        CHECK(ds_armv6_step(&f.cpu,&f.image)==-3 && f.cpu.r[1]==bad_addr[i]);
        CHECK(f.cpu.pc==0x1000 && f.cpu.steps==0 && !memcmp(saved,f.words,sizeof(saved)));
    }
    reset(&f,0x01d100b0u,0); /* failed EQ skips even invalid address */
    CHECK(ds_armv6_step(&f.cpu,&f.image)==0 && f.cpu.pc==0x1004);
    return 0;
}

static int unsupported(void) {
    Fixture f; reset(&f,0xe1c100d0u,0); /* LDRD remains unsupported */
    f.cpu.r[0]=0x1234u;
    CHECK(ds_armv6_step(&f.cpu,&f.image)==-2 && f.cpu.r[0]==0x1234u);
    reset(&f,0xe10f0000u,0); /* MRS must not be interpreted as TST */
    CHECK(ds_armv6_step(&f.cpu,&f.image)==-2);
    return 0;
}

static int vfp_transfers(void) {
    Fixture f; reset(&f,0xed2d8b06u,0); /* vpush {d8-d10} */
    f.cpu.r[13]=0x1100;
    for(unsigned i=0;i<32;++i) f.cpu.vfp[i]=0x12340000u+i;
    CHECK(ds_armv6_step(&f.cpu,&f.image)==0 && f.cpu.r[13]==0x10e8);
    for(unsigned i=0;i<6;++i) CHECK(f.words[58+i]==0x12340010u+i);
    f.words[1]=0xecbd8b06u; /* vpop {d8-d10} */
    memset(f.cpu.vfp,0,sizeof(f.cpu.vfp));
    CHECK(ds_armv6_step(&f.cpu,&f.image)==0 && f.cpu.r[13]==0x1100);
    CHECK(f.cpu.vfp[16]==0x12340010 && f.cpu.vfp[21]==0x12340015);
    reset(&f,0xedd10a01u,0); /* vldr s1,[r1,#4] */
    f.cpu.r[1]=0x1080; f.words[33]=0x7fc01234;
    CHECK(ds_armv6_step(&f.cpu,&f.image)==0 && f.cpu.vfp[1]==0x7fc01234 && f.cpu.vfp[0]==0);
    reset(&f,0xed910b02u,0); f.cpu.r[1]=0x1080; f.words[34]=0x89abcdef; f.words[35]=0x01234567;
    CHECK(ds_armv6_step(&f.cpu,&f.image)==0 && f.cpu.vfp[0]==0x89abcdef && f.cpu.vfp[1]==0x01234567);
    f.words[1]=0xed010b02u; /* vstr d0,[r1,#-8] */
    CHECK(ds_armv6_step(&f.cpu,&f.image)==0 && f.words[30]==0x89abcdef && f.words[31]==0x01234567);
    reset(&f,0xed9f0a00u,0); f.words[2]=0x80000000;
    CHECK(ds_armv6_step(&f.cpu,&f.image)==0 && f.cpu.vfp[0]==0x80000000);
    reset(&f,0xeca10a04u,0); f.cpu.r[1]=0x1080;
    for(unsigned i=0;i<4;++i) f.cpu.vfp[i]=i+1;
    CHECK(ds_armv6_step(&f.cpu,&f.image)==0 && f.cpu.r[1]==0x1090 && f.words[35]==4);
    f.words[1]=0xecb10a04u;f.cpu.r[1]=0x1080;memset(f.cpu.vfp,0,sizeof(f.cpu.vfp));
    CHECK(ds_armv6_step(&f.cpu,&f.image)==0 && f.cpu.r[1]==0x1090 && f.cpu.vfp[3]==4);
    /* Reject complete out-of-range transfers before any register/memory mutation. */
    reset(&f,0xed2d8b06u,0);f.cpu.r[13]=0x1010;f.cpu.vfp[16]=42;
    CHECK(ds_armv6_step(&f.cpu,&f.image)==-3 && f.cpu.r[13]==0x1010 && f.words[0]==0xed2d8b06);
    reset(&f,0xed910b02u,0);f.cpu.r[1]=0x10f4;f.cpu.vfp[0]=42;
    CHECK(ds_armv6_step(&f.cpu,&f.image)==-3 && f.cpu.vfp[0]==42);
    reset(&f,0xed910b02u,0);f.cpu.r[1]=0xfffffffcu;
    CHECK(ds_armv6_step(&f.cpu,&f.image)==-3);
    reset(&f,0xedd10a01u,0);f.cpu.r[1]=0x1081;
    CHECK(ds_armv6_step(&f.cpu,&f.image)==-3);
    const uint32_t bad[]={0xed6d0b02u,0xed2d8b03u,0xecb1fa04u,0xecb10a00u,0xee300a00u};
    for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);++i) {
        reset(&f,bad[i],0);f.cpu.r[1]=0x1080;f.cpu.r[13]=0x1100;
        CHECK(ds_armv6_step(&f.cpu,&f.image)==-2 && f.cpu.pc==0x1000 && f.cpu.steps==0);
    }
    reset(&f,0x0d2d8b06u,0);f.cpu.r[13]=0; /* failed EQ skips transfer */
    CHECK(ds_armv6_step(&f.cpu,&f.image)==0 && f.cpu.r[13]==0);
    return 0;
}

int main(int argc, char **argv) {
    const struct { const char *name; int (*run)(void); } tests[] = {
        {"halfword_transfers",halfword_transfers},{"vfp_transfers",vfp_transfers},{"selftest",selftest},{"fetch_bounds",fetch_bounds},{"flags_only",flags_only},
        {"arithmetic_flags",arithmetic_flags},{"carry_arithmetic",carry_arithmetic},
        {"register_shifts",register_shifts},{"rrx",rrx},{"arm_branches",arm_branches},
        {"blx_immediate",blx_immediate},{"thumb_immediates",thumb_immediates},
        {"thumb_branches",thumb_branches},{"memory_bounds",memory_bounds},{"unsupported",unsupported}
    };
    for (unsigned i=0; i<sizeof(tests)/sizeof(tests[0]); ++i) {
        if (argc==1 || (argc==2 && !strcmp(argv[1],tests[i].name))) {
            if (tests[i].run()) return 1;
            if (argc==2) return 0;
        }
    }
    return argc==1 ? 0 : 2;
}
