#include "../src/runtime/log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x); exit(1); } } while (0)
int main(void) {
    char temp[]="/tmp/dspsp-log-XXXXXX", previous[4096];
    CHECK(getcwd(previous,sizeof(previous)) && mkdtemp(temp) && !chdir(temp));
    CHECK(!mkdir("ms0:",0700) && !mkdir("ms0:/PSP",0700) &&
          !mkdir("ms0:/PSP/GAME",0700) && !mkdir("ms0:/PSP/GAME/DEADSPACE",0700));
    CHECK(dspsp_log_write("a",1)==-1);
    dspsp_log_init();
    char big[2048]; memset(big,'x',sizeof(big)-1); big[sizeof(big)-1]=0;
    dspsp_log("%s",big);
    const char raw[]={0,'A','\n'};
    CHECK(dspsp_log_write(raw,sizeof(raw))==sizeof(raw)); dspsp_log_close();
    CHECK(dspsp_log_write(raw,sizeof(raw))==-1);
    FILE *f=fopen("ms0:/PSP/GAME/DEADSPACE/psp_port.log","rb"); CHECK(f);
    char buf[600]; size_t size=fread(buf,1,sizeof(buf),f); CHECK(!fclose(f));
    CHECK(size==515 && !memcmp(buf,"[DSPSP] ",8) && buf[511]=='\n' && !memcmp(buf+512,raw,3));
    CHECK(!unlink("ms0:/PSP/GAME/DEADSPACE/psp_port.log"));
    CHECK(!rmdir("ms0:/PSP/GAME/DEADSPACE") && !rmdir("ms0:/PSP/GAME") && !rmdir("ms0:/PSP") && !rmdir("ms0:"));
    CHECK(!chdir(previous) && !rmdir(temp)); return 0;
}
