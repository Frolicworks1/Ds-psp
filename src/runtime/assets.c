#include "assets.h"
#include <limits.h>
#include <string.h>
#include <sys/stat.h>

void ds_assets_reset(DsAssets *a) {
    if (!a) return;
    for (unsigned i=0;i<DS_ASSET_SLOTS;++i) if (a->files[i]) fclose(a->files[i]);
    memset(a,0,sizeof(*a));
}
int ds_assets_mount(DsAssets *a,const char *root) {
    struct stat st;
    if (!a || !root || !*root || strlen(root)>=sizeof(a->root) ||
        stat(root,&st) || !S_ISDIR(st.st_mode)) return -1;
    /* No replacement while streams or a manager may refer to this root. */
    if (a->root[0]) return -1;
    strcpy(a->root,root);
    return 0;
}
static int valid_name(const char *s) {
    if (!s || !*s || *s=='/') return 0;
    while (*s) {
        const char *start=s;
        while (*s && *s!='/') {
            if (*s=='\\' || *s==':') return 0;
            ++s;
        }
        size_t n=(size_t)(s-start);
        if (!n || (n==1 && start[0]=='.') || (n==2 && start[0]=='.' && start[1]=='.')) return 0;
        if (*s && !*++s) return 0;
    }
    return 1;
}
int ds_assets_open(DsAssets *a,const char *name) {
    char path[1024];struct stat st;
    if (!a || !a->root[0] || !valid_name(name)) return -1;
    int n=snprintf(path,sizeof(path),"%s/%s",a->root,name);
    if (n<0 || (size_t)n>=sizeof(path) || stat(path,&st) || !S_ISREG(st.st_mode)) return -1;
    for (unsigned i=0;i<DS_ASSET_SLOTS;++i) if (!a->files[i]) {
        a->files[i]=fopen(path,"rb");
        return a->files[i]?(int)i+1:-1;
    }
    return -1;
}
static FILE *stream(DsAssets *a,unsigned slot) {
    return a && slot && slot<=DS_ASSET_SLOTS?a->files[slot-1]:NULL;
}
int ds_assets_read(DsAssets *a,unsigned slot,void *out,size_t size) {
    FILE *f=stream(a,slot);
    if (!f || (!out && size) || size>INT_MAX) return -1;
    if (!size) return 0;
    size_t n=fread(out,1,size,f);
    return ferror(f)?-1:(int)n; /* zero is EOF; JNI translates to -1 */
}
int ds_assets_skip(DsAssets *a,unsigned slot,uint32_t bytes) {
    FILE *f=stream(a,slot);
    if (!f || bytes>INT_MAX) return -1;
    long pos=ftell(f);
    if (pos<0 || fseek(f,0,SEEK_END)) return -1;
    long end=ftell(f);
    if (end<pos) { fseek(f,pos,SEEK_SET);return -1; }
    uint32_t count=(unsigned long)(end-pos)<bytes?(uint32_t)(end-pos):bytes;
    if (fseek(f,pos+(long)count,SEEK_SET)) return -1;
    return (int)count;
}
int ds_assets_close(DsAssets *a,unsigned slot) {
    FILE *f=stream(a,slot);
    if (!f) return -1;
    a->files[slot-1]=NULL;
    return fclose(f)?-1:0;
}
