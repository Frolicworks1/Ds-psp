#define _POSIX_C_SOURCE 200809L
#include "../src/runtime/reloc.h"
#include <elf.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static unsigned char file[1024];
static Elf32_Ehdr eh;
static Elf32_Phdr ph;
static Elf32_Shdr sh[4];
static Elf32_Sym syms[2];
static void write_fixture(const char *path) {
 memcpy(file,&eh,sizeof(eh));memcpy(file+64,&ph,sizeof(ph));memcpy(file+128,sh,sizeof(sh));memcpy(file+512,syms,sizeof(syms));
 FILE *f=fopen(path,"wb");CHECK(f && fwrite(file,1,sizeof(file),f)==sizeof(file));CHECK(!fclose(f));
}
int main(void) {
 char path[]="/tmp/ds-exports-XXXXXX";int fd=mkstemp(path);CHECK(fd>=0);close(fd);
 memcpy(eh.e_ident,ELFMAG,SELFMAG);eh.e_ident[EI_CLASS]=ELFCLASS32;eh.e_ident[EI_DATA]=ELFDATA2LSB;
 eh.e_machine=EM_ARM;eh.e_phoff=64;eh.e_phentsize=sizeof(ph);eh.e_phnum=1;eh.e_shoff=128;eh.e_shentsize=sizeof(*sh);eh.e_shnum=4;
 ph=(Elf32_Phdr){.p_type=PT_LOAD,.p_offset=0,.p_vaddr=0x1000,.p_filesz=1024,.p_memsz=1024,.p_flags=PF_R|PF_X};
 sh[1]=(Elf32_Shdr){.sh_type=SHT_DYNSYM,.sh_offset=512,.sh_size=sizeof(syms),.sh_link=2,.sh_entsize=sizeof(*syms)};
 sh[2]=(Elf32_Shdr){.sh_type=SHT_STRTAB,.sh_offset=600,.sh_size=12};
 memcpy(file+600,"\0JNI_OnLoad\0",12);
 syms[1]=(Elf32_Sym){.st_name=1,.st_value=0x1301,.st_size=4,.st_info=ELF32_ST_INFO(STB_GLOBAL,STT_FUNC),.st_shndx=3};
 DsArmImage image={.image=file,.image_base=0x1000,.image_size=1024};uint32_t result;
 write_fixture(path);CHECK(!ds_arm_find_export(path,&image,"JNI_OnLoad",&result) && result==0x1301);
 CHECK(ds_arm_find_export(path,&image,"missing",&result)==-2 && !result);
 ph.p_flags=PF_R;write_fixture(path);CHECK(ds_arm_find_export(path,&image,"JNI_OnLoad",&result)==-1);ph.p_flags|=PF_X;
 syms[1].st_value=0x1400;write_fixture(path);CHECK(ds_arm_find_export(path,&image,"JNI_OnLoad",&result)==-1);syms[1].st_value=0x1300;
 syms[1].st_name=12;write_fixture(path);CHECK(ds_arm_find_export(path,&image,"JNI_OnLoad",&result)==-1);syms[1].st_name=1;
 syms[1].st_shndx=SHN_UNDEF;write_fixture(path);CHECK(ds_arm_find_export(path,&image,"JNI_OnLoad",&result)==-2);syms[1].st_shndx=3;
 sh[1].sh_link=99;write_fixture(path);CHECK(ds_arm_find_export(path,&image,"JNI_OnLoad",&result)==-1);
 unlink(path);puts("export resolution, Thumb bit, missing/undefined symbols and bounds passed");return 0;
}
