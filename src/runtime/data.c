#include "data.h"
#include "log.h"
#include <pspiofilemgr.h>
#include <stdio.h>
#include <string.h>
static char g_root[128]="ms0:/data/deadspace";
int dspsp_data_init(void){
    SceUID d=sceIoDopen(g_root);
    if(d>=0){sceIoDclose(d); dspsp_log("data root: %s",g_root); return 0;}
    dspsp_log("data root missing: %s (install staged data from your own APK)",g_root);
    return -1;
}
int dspsp_data_exists(const char *relative){
    char p[256]; snprintf(p,sizeof(p),"%s/%s",g_root,relative);
    SceUID f=sceIoOpen(p,PSP_O_RDONLY,0);
    if(f<0) return 0; sceIoClose(f); return 1;
}
const char *dspsp_data_path(const char *relative){
    static char p[256]; snprintf(p,sizeof(p),"%s/%s",g_root,relative); return p;
}
