#include <pspkernel.h>
#include <pspdisplay.h>
#include <pspgu.h>
#include <pspctrl.h>
PSP_MODULE_INFO("DeadSpacePSP", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);
static unsigned int __attribute__((aligned(16))) list[262144];
static unsigned int __attribute__((aligned(16))) framebuf[480 * 272];
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
    init_gu();
    SceCtrlData pad;
    while (1) {
        sceCtrlPeekBufferPositive(&pad, 1);
        if (pad.Buttons & PSP_CTRL_HOME) break;
        sceDisplayWaitVblankStart();
    }
    sceGuTerm();
    sceKernelExitGame();
    return 0;
}
