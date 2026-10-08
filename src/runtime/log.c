#include "log.h"
#include <pspiofilemgr.h>
#include <stdarg.h>
#include <stdio.h>

static SceUID g_log=-1;

void dspsp_log_init(void){
    g_log=sceIoOpen("ms0:/PSP/GAME/DEADSPACE/psp_port.log",
                    PSP_O_WRONLY|PSP_O_CREAT|PSP_O_TRUNC,0777);
}

void dspsp_log(const char *fmt,...){
    if(g_log<0) return;
    char b[512];
    int p=snprintf(b,sizeof(b),"[DSPSP] ");
    if(p<0) return;
    if(p>(int)sizeof(b)) p=(int)sizeof(b);
    va_list ap;
    va_start(ap,fmt);
    if(p<(int)sizeof(b)) p+=vsnprintf(b+p,sizeof(b)-p,fmt,ap);
    va_end(ap);
    if(p>0) sceIoWrite(g_log,b,p);
    sceIoWrite(g_log,"\n",1);
}

void dspsp_log_close(void){
    if(g_log>=0){ sceIoClose(g_log); g_log=-1; }
}
