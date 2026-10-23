#include <pspkernel.h>
#include <pspdisplay.h>
#include <pspdebug.h>
#include <pspctrl.h>
#include "runtime/runtime.h"
#include "runtime/log.h"
#include "runtime/arm_loader.h"
#include "runtime/reloc.h"
#include "runtime/imports.h"
#include "cpu/armv6.h"

PSP_MODULE_INFO("DeadSpaceDiagnostic", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);

static volatile int exit_requested;
static int exit_callback(int arg1, int arg2, void *common) {
    (void)arg1; (void)arg2; (void)common;
    exit_requested = 1;
    return 0;
}
static int callback_thread(SceSize args, void *argp) {
    (void)args; (void)argp;
    int callback = sceKernelCreateCallback("Exit", exit_callback, NULL);
    if (callback >= 0) sceKernelRegisterExitCallback(callback);
    sceKernelSleepThreadCB();
    return 0;
}

int main(void) {
    pspDebugScreenInit();
    pspDebugScreenPrintf("Dead Space - Unofficial PSP Port\n");
    pspDebugScreenPrintf("Diagnostic Test 1 - NOT PLAYABLE\n\n");
    pspDebugScreenPrintf("No game menu, graphics, audio or gameplay yet.\n");
    int thread = sceKernelCreateThread("ExitThread", callback_thread, 0x11, 0x1000, 0, NULL);
    if (thread >= 0) sceKernelStartThread(thread, 0, NULL);
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_DIGITAL);
    dspsp_runtime_init();
    ds_imports_init();
    dspsp_log("Unofficial PSP Diagnostic Test 1; not playable");

    int cpu_test = ds_armv6_selftest();
    pspDebugScreenPrintf("\nARM CPU self-test: %s (%d)\n", cpu_test ? "FAIL" : "PASS", cpu_test);
    dspsp_log("ARM CPU self-test=%d", cpu_test);

    /* Optional original library; no game assets or save files are modified. */
    const char *donor = "ms0:/data/deadspace/guest_libs/libDeadSpace.so";
    DsArmImage image = {0};
    int loaded = ds_arm_loader_load(donor, &image);
    pspDebugScreenPrintf("Donor load: %d\n", loaded);
    dspsp_log("donor load=%d image_bytes=%u arena_bytes=%u", loaded, image.image_size, image.heap_size);
    if (!loaded) {
        ds_imports_bind_image(&image);
        int relocated = ds_arm_apply_relocations(donor, &image);
        pspDebugScreenPrintf("Linking: %d; imports: %u\n", relocated, ds_import_resolve_count());
        dspsp_log("relocation=%d imports=%u", relocated, ds_import_resolve_count());
        if (!relocated && !cpu_test) {
            DsArmv6Cpu cpu;
            ds_armv6_reset(&cpu, image.entry);
            cpu.r[13] = image.stack_base + image.stack_size - 16u;
            DsArmv6Image memory = {image.image, image.image_size + image.heap_size, image.image_base};
            int rc = ds_armv6_run(&cpu, &memory, 1);
            pspDebugScreenPrintf("One-instruction probe: %d, PC %08X\n", rc, (unsigned)cpu.pc);
            dspsp_log("single-step rc=%d pc=%08X", rc, (unsigned)cpu.pc);
        }
    } else if (loaded == -2) {
        pspDebugScreenPrintf("Optional game library not found.\n");
        pspDebugScreenPrintf("CPU/screen test can run without game data.\n");
    } else {
        pspDebugScreenPrintf("Library rejected or insufficient memory.\n");
    }
    ds_imports_bind_image(NULL);
    ds_arm_loader_unload(&image);
    pspDebugScreenPrintf("\nThis test does NOT initialize the game.\n");
    pspDebugScreenPrintf("Log (if writable):\nms0:/PSP/GAME/DEADSPACE/psp_port.log\n");
    pspDebugScreenPrintf("\nPlease report your PSP model / PPSSPP version,\n");
    pspDebugScreenPrintf("a photo of this screen, and the log.\n\n");
    pspDebugScreenPrintf("Press START to exit.\n");
    while (!exit_requested) {
        SceCtrlData pad = {0};
        sceCtrlPeekBufferPositive(&pad, 1);
        if (pad.Buttons & PSP_CTRL_START) break;
        sceDisplayWaitVblankStart();
    }
    dspsp_runtime_shutdown();
    sceKernelExitGame();
    return 0;
}
