/* Bounded execution of relocated guest bytes; unsupported imports stop. */
#include "../src/cpu/armv6.h"
#include "../src/runtime/arm_loader.h"
#include "../src/runtime/reloc.h"
#include "../src/runtime/imports.h"
#include "../src/input/controls.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int run_routine(DsArmImage *mapped, uint32_t entry, unsigned long limit,
                       int verbose, unsigned long long *steps, const uint32_t *args, unsigned arg_count, uint32_t *result) {
    DsArmv6Image memory={mapped->image,mapped->image_size+mapped->heap_size,mapped->image_base};
    DsArmv6Cpu cpu;
    ds_armv6_reset(&cpu,entry);
    cpu.r[13]=mapped->stack_base+mapped->stack_size-16u;
    cpu.r[14]=0xfffffff0u;
    if (arg_count>7 || (arg_count && !args)) return 1;
    if (args) for (unsigned a=0;a<arg_count;++a) {
        if (a<4) cpu.r[a]=args[a];
        else memcpy(mapped->image+cpu.r[13]-mapped->image_base+(a-4)*4,args+a,4);
    }

    printf("image_base=0x%08x image_bytes=%u entry=0x%08x heap_bytes=%u\n",
           mapped->image_base,mapped->image_size,entry,mapped->heap_size);
    int status=2;
    for (unsigned i=0;i<=limit;++i) {
        if (cpu.pc==0) { printf("STOP null branch target; relocation/import resolution required.\n"); break; }
        if (cpu.pc==0xfffffff0u) { printf("Guest routine returned to sentinel.\n"); status=0; break; }
        if (i==limit) { printf("STOP bounded instruction limit reached.\n"); break; }
        uint32_t instruction=0, offset=cpu.pc-memory.load_addr;
        unsigned width=cpu.thumb?2u:4u;
        if (cpu.pc>=memory.load_addr && offset<=memory.size && memory.size-offset>=width)
            memcpy(&instruction,memory.base+offset,width);
        if (verbose) printf("step=%u pc=0x%08x mode=%s opcode=0x%08x\n",i,cpu.pc,cpu.thumb?"Thumb":"ARM",instruction);
        int rc=ds_armv6_step(&cpu,&memory);
        if (rc) {
            printf("STOP error=%d pc=%08x opcode=%08x after_steps=%u r0=%08x r1=%08x r2=%08x r3=%08x sp=%08x\n",
                   rc,cpu.pc,instruction,cpu.steps,cpu.r[0],cpu.r[1],cpu.r[2],cpu.r[3],cpu.r[13]);
            break;
        }

    }
    if (result) *result=cpu.r[0];
    *steps += cpu.steps;
    return status;
}

int main(int argc, char **argv) {
    if (argc < 2 || argc > 5) {
        fprintf(stderr,"Usage: %s <guest.so> [entry-virtual-address|constructors|startup|startup-controls [per-routine-instruction-limit [mmap-MiB]]]\n",argv[0]);
        return 64;
    }
    DsArmImage mapped;
    uint32_t mmap_bytes=2u*1024u*1024u;
    if (argc==5) {
        char *end; errno=0;
        unsigned long mib=strtoul(argv[4],&end,0);
        if (errno || !argv[4][0] || *end || !mib || mib>32) return 64;
        mmap_bytes=(uint32_t)mib*1024u*1024u;
    }
    int rc=ds_arm_loader_load_with_mmap(argv[1],&mapped,mmap_bytes);
    if (rc) { fprintf(stderr,"loader error=%d\n",rc); return 1; }
    ds_imports_init();
    ds_imports_bind_image(&mapped);
    rc = ds_arm_apply_relocations(argv[1], &mapped);
    if (rc) { fprintf(stderr, "relocation error=%d\n", rc); ds_arm_loader_unload(&mapped); return 1; }
    uint32_t entry=mapped.entry;
    int control_probe=argc>=3 && !strcmp(argv[2],"startup-controls");
    int startup=control_probe || (argc>=3 && !strcmp(argv[2],"startup"));
    int constructors=startup || (argc>=3 && !strcmp(argv[2],"constructors"));
    if (argc>=3 && !constructors) {
        char *end; errno=0;
        unsigned long value=strtoul(argv[2],&end,0);
        if (errno || !argv[2][0] || *end || value>UINT32_MAX) {
            ds_arm_loader_unload(&mapped); return 64;
        }
        entry=(uint32_t)value;
    }
    unsigned long limit=32;
    if (argc>=4) {
        char *end; errno=0;
        limit=strtoul(argv[3],&end,0);
        if (errno || !argv[3][0] || *end || limit==0 || limit>100000) {
            ds_arm_loader_unload(&mapped); return 64;
        }
    }
    unsigned long long steps=0;
    int status=0;
    if (constructors) {
        printf("INITIALIZERS init=%08x array=%08x count=%u\n",mapped.init_function,mapped.init_array,mapped.init_count);
        if (mapped.init_function) {
            printf("DT_INIT entry=%08x\n",mapped.init_function);
            status=run_routine(&mapped,mapped.init_function,limit,0,&steps,NULL,0,NULL);
        }
        unsigned returned=0, skipped=0, attempted=0;
        for (unsigned i=0; !status && i<mapped.init_count; ++i) {
            uint32_t target;
            memcpy(&target,mapped.image+mapped.init_array-mapped.image_base+i*4,4);
            if (!target || target==UINT32_MAX) { ++skipped; continue; }
            ++attempted;
            printf("CONSTRUCTOR index=%u entry=%08x\n",i,target);
            if ((target & ~1u)<mapped.image_base || (target & ~1u)-mapped.image_base>=mapped.image_size) {
                printf("STOP initializer target outside image\n"); status=2; break;
            }
            status=run_routine(&mapped,target,limit,0,&steps,NULL,0,NULL);
            if (!status) ++returned;
        }
        printf("CONSTRUCTORS total=%u attempted=%u returned=%u skipped=%u pending=%u\n",
               mapped.init_count,attempted,returned,skipped,mapped.init_count-attempted-skipped);
    } else status=run_routine(&mapped,entry,limit,1,&steps,NULL,0,NULL);
    if (!status && startup) {
        uint32_t vm,env,target,result,activity=0,args[4]={0};
        if (ds_imports_prepare_jni(&vm,&env) ||
            ds_arm_find_export(argv[1],&mapped,"JNI_OnLoad",&target)) status=1;
        else {
            args[0]=vm;
            printf("STARTUP JNI_OnLoad entry=%08x vm=%08x env=%08x\n",target,vm,env);
            status=run_routine(&mapped,target,limit,0,&steps,args,4,&result);
            if (!status && result!=0x10004 && result!=0x10006) status=2;
            printf("JNI_OnLoad result=%08x status=%d\n",result,status);
        }
        if (!status) {
            if (ds_imports_start_activity(&activity) ||
                ds_arm_find_export(argv[1],&mapped,"Java_com_ea_blast_MainActivity_NativeOnCreate",&target)) status=1;
            else {
                args[0]=env;args[1]=activity;
                printf("STARTUP NativeOnCreate entry=%08x\n",target);
                status=run_routine(&mapped,target,limit,0,&steps,args,4,&result);
                printf("NativeOnCreate status=%d\n",status);
            }
        }
        const struct { const char *name; uint32_t a,b; } stages[]={
            {"Java_com_ea_blast_AndroidRenderer_NativeOnSurfaceCreated",0,0},
            {"Java_com_ea_blast_AndroidRenderer_NativeOnSurfaceChanged",480,272},
            {"Java_com_ea_blast_KeyboardAndroid_NativeOnVisibilityChanged",600,1},
            {"Java_com_ea_blast_MainActivity_NativeOnWindowFocusChanged",1,0},
            {"Java_com_ea_blast_AndroidRenderer_NativeOnDrawFrame",0,0}
        };
        for (unsigned i=0;!status && i<sizeof(stages)/sizeof(*stages);++i) {
            if (i==4 && control_probe) {
                DsControls controls={0};DsInputBatch batch;
                const DsPad samples[]={{DS_PAD_L|DS_PAD_TRIANGLE,255,128},{0,128,128}};
                unsigned delivered=0;
                for (unsigned sample=0;!status && sample<2;++sample) {
                    if (ds_controls_update(&controls,samples[sample],480,272,1,&batch)) {status=1;break;}
                    for (unsigned event=0;!status && event<batch.count;++event) {
                        const char *symbol;uint32_t words[7];
                        unsigned count=ds_controls_pack(&batch.events[event],env,0,&symbol,words);
                        if (!count || ds_arm_find_export(argv[1],&mapped,symbol,&target)) {status=1;break;}
                        status=run_routine(&mapped,target,limit,0,&steps,words,count,&result);
                        if (!status) ++delivered;
                    }
                }
                printf("CONTROL_CALLBACKS returned=%u status=%d (game initialization still gates input)\n",delivered,status);
                if (status) break;
            }
            if (ds_arm_find_export(argv[1],&mapped,stages[i].name,&target)) {status=1;break;}
            args[0]=env;args[1]=i==3?activity:0;args[2]=stages[i].a;args[3]=stages[i].b;
            printf("STARTUP %s entry=%08x\n",stages[i].name,target);
            status=run_routine(&mapped,target,limit,0,&steps,args,4,&result);
            printf("STARTUP stage=%u status=%d\n",i,status);
        }
    }
    DsGuestMemoryStats stats=ds_import_memory_stats();
    printf("MEMORY mapped_total=%u bridge_used=%u brk_used=%u mmap_budget=%u mmap_live=%u mmap_peak=%u largest_request=%u calls=%u failures=%u steps=%llu\n",
        mapped.image_size+mapped.heap_size,mapped.heap_used,mapped.brk_used,mapped.mmap_size,
        stats.mmap_live_bytes,stats.mmap_peak_bytes,stats.mmap_largest_request,
        stats.mmap_calls,stats.mmap_failures,steps);
    ds_arm_loader_unload(&mapped);
    return status;
}
