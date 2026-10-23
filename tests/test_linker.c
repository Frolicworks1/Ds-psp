#include "../src/runtime/reloc.h"
#include "../src/runtime/imports.h"
#include "../src/cpu/armv6.h"
#include <elf.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "bionic_ctype_expected.h"
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %d: %s\n", __LINE__, #x); exit(1); } } while (0)
static unsigned char file[2048], memory[2048];
static char path[] = "/tmp/dspsp-link-XXXXXX";
static void put(unsigned offset, const void *value, size_t size) { memcpy(file + offset, value, size); }
static void fixture(unsigned type, unsigned symbol_type) {
    memset(file, 0, sizeof(file)); memset(memory, 0, sizeof(memory));
    Elf32_Ehdr eh = {0};
    memcpy(eh.e_ident, ELFMAG, SELFMAG);
    eh.e_ident[EI_CLASS] = ELFCLASS32; eh.e_ident[EI_DATA] = ELFDATA2LSB;
    eh.e_ident[EI_VERSION] = EV_CURRENT; eh.e_version = EV_CURRENT;
    eh.e_type = ET_DYN; eh.e_machine = EM_ARM; eh.e_ehsize = sizeof(eh);
    eh.e_phoff = sizeof(eh); eh.e_phentsize = sizeof(Elf32_Phdr); eh.e_phnum = 2;
    put(0, &eh, sizeof(eh));
    Elf32_Phdr ph = { .p_type=PT_LOAD, .p_vaddr=0x1000, .p_filesz=sizeof(file), .p_memsz=sizeof(file) };
    put(sizeof(eh), &ph, sizeof(ph));
    Elf32_Dyn dyn[] = {
        { .d_tag=DT_REL, .d_un.d_ptr=0x1200 },
        { .d_tag=DT_RELSZ, .d_un.d_val=sizeof(Elf32_Rel) },
        { .d_tag=DT_SYMTAB, .d_un.d_ptr=0x1300 },
        { .d_tag=DT_STRTAB, .d_un.d_ptr=0x1400 },
        { .d_tag=DT_STRSZ, .d_un.d_val=8 },
        { .d_tag=DT_NULL }
    };
    ph = (Elf32_Phdr){ .p_type=PT_DYNAMIC, .p_offset=0x100, .p_filesz=sizeof(dyn) };
    put(sizeof(eh)+sizeof(ph), &ph, sizeof(ph)); put(0x100, dyn, sizeof(dyn));
    Elf32_Rel rel = { .r_offset=0x1500, .r_info=ELF32_R_INFO(type==R_ARM_RELATIVE?0:1,type) };
    put(0x200, &rel, sizeof(rel));
    Elf32_Sym sym = { .st_name=1, .st_info=ELF32_ST_INFO(STB_GLOBAL,symbol_type), .st_shndx=SHN_UNDEF };
    put(0x300+sizeof(sym), &sym, sizeof(sym)); put(0x400,"\0malloc",8);
    uint32_t addend=7; memcpy(memory+0x500,&addend,4);
}
static int apply(DsArmImage *im) {
    FILE *f=fopen(path,"wb"); CHECK(f); CHECK(fwrite(file,1,sizeof(file),f)==sizeof(file)); CHECK(!fclose(f));
    return ds_arm_apply_relocations(path,im);
}
static uint32_t word(void) { uint32_t v; memcpy(&v,memory+0x500,4); return v; }
static uint32_t le32(const unsigned char *p) {
    return (uint32_t)p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24;
}
static void test_data_imports(void) {
    unsigned char arena[4096] = {0};
    DsArmImage im={.image=arena,.image_base=0x1000,.image_size=1536,
                   .heap_base=0x1600,.heap_size=2560};
    ds_imports_init(); ds_imports_bind_image(&im);
    const char *names[]={"_ctype_", "_tolower_tab_", "_toupper_tab_"};
    uint32_t addresses[3];
    for (unsigned kind=0; kind<3; ++kind) {
        uint32_t addr=ds_import_resolve_data(names[kind]); addresses[kind]=addr;
        CHECK(addr>=im.heap_base && addr<im.heap_base+im.heap_size);
        uint32_t used=im.heap_used;
        CHECK(ds_import_resolve_data(names[kind])==addr && im.heap_used==used);
        uint32_t table=le32(arena+addr-im.image_base);
        CHECK(table==addr+4 && !ds_import_is_trampoline(addr));
        for (unsigned i=0; i<257; ++i) {
            unsigned char *p=arena+table-im.image_base+i*(kind?2:1);
            unsigned actual=kind ? (unsigned)p[0] | (unsigned)p[1]<<8 : p[0];
            unsigned expected=kind==0 ? expected_ctype[i] :
                (uint16_t)(kind==1 ? expected_lower[i] : expected_upper[i]);
            CHECK(actual==expected);
        }
    }
    CHECK(ds_import_resolve_count()==0);
    CHECK(ds_import_resolve_data("__sF")==0 && ds_import_resolve_data(NULL)==0);
    /* Relocation must store the address of the pointer variable, not its value. */
    fixture(R_ARM_GLOB_DAT,STT_OBJECT);
    put(0x400,"\0_ctype_",9);
    Elf32_Dyn dyn={.d_tag=DT_STRSZ,.d_un.d_val=9}; put(0x120,&dyn,sizeof(dyn));
    CHECK(apply(&im)==0); CHECK(le32(arena+0x500)==addresses[0]);
    /* No heap consumption on exhaustion; initialize registry for each image. */
    ds_imports_init(); ds_imports_bind_image(&im); im.heap_size=8; im.heap_used=0;
    CHECK(ds_import_resolve_data("_ctype_")==0 && im.heap_used==0);
    ds_imports_bind_image(NULL); CHECK(ds_import_resolve_data("_ctype_")==0);
    ds_imports_init();
}
int main(void) {
    int fd=mkstemp(path); CHECK(fd>=0); close(fd);
    DsArmImage im={ .image=memory, .image_base=0x1000, .image_size=1024+512, .heap_base=0x1600, .heap_size=512 };
    ds_imports_init(); ds_imports_bind_image(&im);
    fixture(R_ARM_RELATIVE,STT_FUNC); CHECK(apply(&im)==0); CHECK(word()==7); /* nonzero image base is not bias */
    fixture(R_ARM_ABS32,STT_FUNC); CHECK(apply(&im)==0); CHECK(word()==DS_IMPORT_TRAMP_BASE+7);
    fixture(R_ARM_GLOB_DAT,STT_FUNC); CHECK(apply(&im)==0); CHECK(word()==DS_IMPORT_TRAMP_BASE);
    fixture(R_ARM_JUMP_SLOT,STT_FUNC); CHECK(apply(&im)==0); CHECK(word()==DS_IMPORT_TRAMP_BASE);
    /* Exercise the separate PLT table and reject a malformed PLT format. */
    fixture(R_ARM_JUMP_SLOT,STT_FUNC);
    Elf32_Dyn dyn={.d_tag=DT_JMPREL,.d_un.d_ptr=0x1200}; put(0x100,&dyn,sizeof(dyn));
    dyn=(Elf32_Dyn){.d_tag=DT_PLTRELSZ,.d_un.d_val=sizeof(Elf32_Rel)}; put(0x108,&dyn,sizeof(dyn));
    dyn=(Elf32_Dyn){.d_tag=DT_PLTREL,.d_un.d_val=DT_REL}; put(0x128,&dyn,sizeof(dyn));
    dyn=(Elf32_Dyn){.d_tag=DT_NULL}; put(0x130,&dyn,sizeof(dyn));
    Elf32_Phdr dynamic={.p_type=PT_DYNAMIC,.p_offset=0x100,.p_filesz=56};
    put(sizeof(Elf32_Ehdr)+sizeof(Elf32_Phdr),&dynamic,sizeof(dynamic));
    CHECK(apply(&im)==0 && word()==DS_IMPORT_TRAMP_BASE);
    dyn=(Elf32_Dyn){.d_tag=DT_PLTREL,.d_un.d_val=DT_RELA}; put(0x128,&dyn,sizeof(dyn)); CHECK(apply(&im)==-3);
    fixture(R_ARM_ABS32,STT_FUNC);
    Elf32_Sym sym={.st_value=0x1234,.st_shndx=1}; put(0x300+sizeof(sym),&sym,sizeof(sym));
    CHECK(apply(&im)==0); CHECK(word()==0x123b);
    fixture(R_ARM_ABS32,STT_OBJECT); CHECK(apply(&im)==-10);
    fixture(255,STT_FUNC); CHECK(apply(&im)==-11);
    fixture(R_ARM_ABS32,STT_FUNC); Elf32_Rel bad={.r_offset=0x15ff,.r_info=ELF32_R_INFO(1,R_ARM_ABS32)};
    put(0x200,&bad,sizeof(bad)); CHECK(apply(&im)==-8);
    fixture(R_ARM_ABS32,STT_FUNC); memset(file+0x400,'x',8); CHECK(apply(&im)==-10);
    fixture(R_ARM_ABS32,STT_FUNC); bad=(Elf32_Rel){.r_offset=0x1500,.r_info=ELF32_R_INFO(0xffffff,R_ARM_ABS32)};
    put(0x200,&bad,sizeof(bad)); CHECK(apply(&im)==-10);
    fixture(R_ARM_RELATIVE,STT_FUNC); im.image_size=2; CHECK(apply(&im)==-8); im.image_size=1536;
    char name[32]="strlen";
    uint32_t tr=(uint32_t)(uintptr_t)ds_import_resolve(0,name); strcpy(name,"changed");
    CHECK(tr==(uint32_t)(uintptr_t)ds_import_resolve(0,"strlen"));
    CHECK(!ds_import_is_trampoline(DS_IMPORT_TRAMP_BASE+4*ds_import_resolve_count()));
    CHECK(!ds_import_is_trampoline(tr+1));
    DsArmv6Cpu cpu={0}; cpu.r[0]=0x1000; cpu.r[14]=0x2001;
    memory[0]='a'; memory[1]=0; CHECK(ds_import_call(tr,&cpu)==0); CHECK(cpu.r[0]==1 && cpu.pc==0x2000 && cpu.thumb);
    memset(memory,'x',sizeof(memory)); cpu.r[0]=0x17ff; CHECK(ds_import_call(tr,&cpu)==-31);
    tr=(uint32_t)(uintptr_t)ds_import_resolve(0,"memset"); cpu.r[0]=0x17ff; cpu.r[2]=2; CHECK(ds_import_call(tr,&cpu)==-31);
    tr=(uint32_t)(uintptr_t)ds_import_resolve(0,"malloc"); cpu.r[0]=UINT32_MAX; CHECK(ds_import_call(tr,&cpu)==0 && cpu.r[0]==0);
    cpu.r[0]=8; CHECK(ds_import_call(tr,&cpu)==0 && cpu.r[0]==0x1600); /* relocation preserved binding */
    tr=(uint32_t)(uintptr_t)ds_import_resolve(0,"pthread_create"); cpu.pc=tr; cpu.r[0]=42;
    CHECK(ds_import_call(tr,&cpu)==-32 && cpu.r[0]==42 && cpu.pc==tr);
    CHECK(ds_import_call(tr+1,&cpu)==-1);
    /* Malformed segment arithmetic is rejected before allocation. */
    fixture(R_ARM_RELATIVE,STT_FUNC); CHECK(apply(&im)==0);
    DsArmImage loaded; CHECK(ds_arm_loader_load(path,&loaded)==0); ds_arm_loader_unload(&loaded);
    Elf32_Phdr ph={.p_type=PT_LOAD,.p_vaddr=0xfffffff0,.p_memsz=0x100}; put(sizeof(Elf32_Ehdr),&ph,sizeof(ph));
    CHECK(apply(&im)==-7); CHECK(ds_arm_loader_load(path,&loaded)==-5);
    test_data_imports();
    unlink(path); puts("linker/import bounds and semantics passed"); return 0;
}
