#define _POSIX_C_SOURCE 200809L
#include "../src/runtime/assets.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
int main(void) {
 DsAssets a={0};char root[]="/tmp/ds-assets-XXXXXX",path[128],buf[16];
 CHECK(mkdtemp(root));snprintf(path,sizeof(path),"%s/file",root);
 FILE *f=fopen(path,"wb");CHECK(f && fwrite("abc\0def",1,7,f)==7 && !fclose(f));
 CHECK(ds_assets_mount(&a,path)==-1 && !a.root[0]);
 CHECK(!ds_assets_mount(&a,root) && ds_assets_mount(&a,root)==-1);
 const char *bad[]={"","/file","../file","a/../file","./file","a//b","a/","ms0:/file","a\\b","missing"};
 for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);++i)CHECK(ds_assets_open(&a,bad[i])==-1);
 int slot=ds_assets_open(&a,"file");CHECK(slot>0);
 CHECK(ds_assets_read(&a,slot,NULL,0)==0 && ds_assets_read(&a,slot,NULL,1)==-1);
 CHECK(ds_assets_read(&a,slot,buf,4)==4 && !memcmp(buf,"abc\0",4));
 CHECK(ds_assets_skip(&a,slot,100)==3 && ds_assets_read(&a,slot,buf,4)==0);
 CHECK(ds_assets_skip(&a,slot,1)==0 && !ds_assets_close(&a,slot));
 CHECK(ds_assets_read(&a,slot,buf,1)==-1 && ds_assets_close(&a,slot)==-1);
 CHECK(ds_assets_read(&a,17,buf,1)==-1 && ds_assets_skip(&a,0,1)==-1);
 for(unsigned i=0;i<DS_ASSET_SLOTS;++i)CHECK(ds_assets_open(&a,"file")== (int)i+1);
 CHECK(ds_assets_open(&a,"file")==-1);
 CHECK(ds_assets_read(&a,2,buf,1)==1 && buf[0]=='a');
 CHECK(ds_assets_read(&a,1,buf,1)==1 && buf[0]=='a');
 ds_assets_reset(&a);CHECK(!a.root[0] && ds_assets_read(&a,1,buf,1)==-1);
 CHECK(!ds_assets_mount(&a,root));slot=ds_assets_open(&a,"file");
 CHECK(ds_assets_read(&a,slot,buf,sizeof(buf))==7 && !memcmp(buf,"abc\0def",7));
 ds_assets_reset(&a);CHECK(!unlink(path) && !rmdir(root));
 puts("Bounded asset streams, binary reads, EOF, skip, paths and reset passed");
}
