#include <pspkernel.h>
#include <pspdisplay.h>
#include <pspgu.h>
#include <pspctrl.h>
#include "runtime/donor_elf.h"
#include "runtime/runtime.h"
#include "runtime/log.h"
#include "runtime/arm_elf.h"
#include "runtime/arm_loader.h"
#include "runtime/reloc.h"
#include "runtime/imports.h"
#include "cpu/armv6.h"

PSP_MODULE_INFO("DeadSpacePSP", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);

static unsigned int __attribute__((aligned(16))) list[262144];
static unsigned int __attribute__((aligned(16))) framebuf[512 * 272];

static void init_gu(void) {
    sceGuInit();
    sceGuStart(GU_DIRECT, list);
    sceGuDrawBuffer(GU_PSM_8888, framebuf, 512);
    sceGuDispBuffer(480, 272, framebuf, 512);
    sceGuDepthBuffer(framebuf + 480 * 272, 512);
    sceGuOffset(2048 - 240, 2048 - 136);
    sceGuViewport(2048, 2048, 480, 272);
    sceGuScissor(0, 0, 480, 272);
    sceGuEnable(GU_SCISSOR_TEST);
    sceGuFinish();
    sceGuSync(0, 0);
    sceDisplayWaitVblankStart();
    sceGuDisplay(GU_TRUE);
}

int main(void) {
    char donor_report[128];
    dspsp_runtime_init();
    ds_imports_init();
    dspsp_log("Dead Space PSP bootstrap starting");

    int cpu_test = ds_armv6_selftest();
    dspsp_log("ARMv6 interpreter self-test=%d", cpu_test);

    DsArmElfInfo elf_info;
    int elf_probe = ds_arm_elf_probe("ms0:/data/deadspace/guest_libs/libDeadSpace.so", &elf_info);
    dspsp_log("ARM ELF probe=%d entry=0x%08X base=0x%08X size=%u segments=%u", elf_probe, elf_info.entry, elf_info.image_base, elf_info.image_size, elf_info.load_segments);

    int elf_status = dspsp_inspect_donor(
        "ms0:/data/deadspace/guest_libs/libDeadSpace.so",
        donor_report, sizeof(donor_report));
    dspsp_log("donor status=%d report=%s", elf_status, donor_report);

    DsArmImage arm_image;
    int load_status = ds_arm_loader_load(
        "ms0:/data/deadspace/guest_libs/libDeadSpace.so", &arm_image);
    dspsp_log("ARM image load=%d base=0x%08X size=%u entry=0x%08X segments=%u",
              load_status, arm_image.image_base, arm_image.image_size,
              arm_image.entry, arm_image.load_segments);
    if (load_status == 0) {
        ds_imports_bind_image(&arm_image);
        int reloc_status = ds_arm_apply_relocations(
            "ms0:/data/deadspace/guest_libs/libDeadSpace.so", &arm_image);
        dspsp_log("ARM self-relocations=%d imports=%u", reloc_status, ds_import_resolve_count());
        /* Reserve the top of the guest arena as an ARM-compatible stack. */
        DsArmv6Cpu guest_cpu;
        ds_armv6_reset(&guest_cpu, arm_image.image_base + arm_image.entry);
        guest_cpu.r[13] = arm_image.heap_base + arm_image.heap_size - 16u;
        dspsp_log("guest stack=0x%08X entry=0x%08X", guest_cpu.r[13], guest_cpu.pc);
        int guest_rc = ds_armv6_run(&guest_cpu, &(DsArmv6Image){
            arm_image.image, arm_image.image_size + arm_image.heap_size, arm_image.image_base
        }, 1u);
        dspsp_log("guest entry probe rc=%d pc=0x%08X", guest_rc, guest_cpu.pc);
    }

    init_gu();
    dspsp_log("GU initialized; waiting for HOME");

    SceCtrlData pad;
    while (1) {
        sceCtrlPeekBufferPositive(&pad, 1);
        if (pad.Buttons & PSP_CTRL_HOME) break;
        sceDisplayWaitVblankStart();
    }

    sceGuTerm();
    ds_arm_loader_unload(&arm_image);
    dspsp_runtime_shutdown();
    sceKernelExitGame();
    return 0;
}
