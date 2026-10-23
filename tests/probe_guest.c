/* Bounded execution of relocated guest bytes; unsupported imports stop. */
#include "../src/cpu/armv6.h"
#include "../src/runtime/arm_loader.h"
#include "../src/runtime/reloc.h"
#include "../src/runtime/imports.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
    if (argc < 2 || argc > 4) {
        fprintf(stderr,"Usage: %s <guest.so> [entry-virtual-address [instruction-limit]]\n",argv[0]);
        return 64;
    }
    DsArmImage mapped;
    int rc=ds_arm_loader_load(argv[1],&mapped);
    if (rc) { fprintf(stderr,"loader error=%d\n",rc); return 1; }
    ds_imports_init();
    ds_imports_bind_image(&mapped);
    rc = ds_arm_apply_relocations(argv[1], &mapped);
    if (rc) { fprintf(stderr, "relocation error=%d\n", rc); ds_arm_loader_unload(&mapped); return 1; }
    uint32_t entry=mapped.entry;
    if (argc>=3) {
        char *end; errno=0;
        unsigned long value=strtoul(argv[2],&end,0);
        if (errno || !argv[2][0] || *end || value>UINT32_MAX) {
            ds_arm_loader_unload(&mapped); return 64;
        }
        entry=(uint32_t)value;
    }
    unsigned long limit=32;
    if (argc==4) {
        char *end; errno=0;
        limit=strtoul(argv[3],&end,0);
        if (errno || !argv[3][0] || *end || limit==0 || limit>100000) {
            ds_arm_loader_unload(&mapped); return 64;
        }
    }
    DsArmv6Image memory={mapped.image,mapped.image_size+mapped.heap_size,mapped.image_base};
    DsArmv6Cpu cpu;
    ds_armv6_reset(&cpu,entry);
    cpu.r[13]=mapped.heap_base+mapped.heap_size-16u;
    cpu.r[14]=0xfffffff0u;
    printf("Relocated bounded probe; unsupported imports stop execution; no JNI.\n");
    printf("image_base=0x%08x image_bytes=%u entry=0x%08x heap_bytes=%u\n",
           mapped.image_base,mapped.image_size,entry,mapped.heap_size);
    int status=2;
    for (unsigned i=0;i<limit;++i) {
        if (cpu.pc==0) { printf("STOP null branch target; relocation/import resolution required.\n"); break; }
        if (cpu.pc==0xfffffff0u) { printf("Guest routine returned to sentinel.\n"); status=0; break; }
        uint32_t instruction=0, offset=cpu.pc-memory.load_addr;
        unsigned width=cpu.thumb?2u:4u;
        if (cpu.pc>=memory.load_addr && offset<=memory.size && memory.size-offset>=width)
            memcpy(&instruction,memory.base+offset,width);
        printf("step=%u pc=0x%08x mode=%s opcode=0x%08x\n",i,cpu.pc,cpu.thumb?"Thumb":"ARM",instruction);
        rc=ds_armv6_step(&cpu,&memory);
        if (rc) { printf("STOP error=%d after_steps=%u\n",rc,cpu.steps); break; }
        if (i+1==limit) printf("STOP bounded instruction limit reached.\n");
    }
    ds_arm_loader_unload(&mapped);
    return status;
}
