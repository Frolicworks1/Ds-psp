#ifndef DSPSP_HOST_PSPIO_H
#define DSPSP_HOST_PSPIO_H
/* Host-only file I/O adapter for loader tests; not an Android/JNI runtime. */
#include <fcntl.h>
#include <stdint.h>
#include <unistd.h>
typedef int SceUID;
typedef int64_t SceOff;
#define PSP_O_RDONLY O_RDONLY
#define PSP_O_WRONLY O_WRONLY
#define PSP_O_CREAT O_CREAT
#define PSP_O_TRUNC O_TRUNC
#define PSP_SEEK_SET SEEK_SET
static inline SceUID sceIoOpen(const char *p, int flags, unsigned mode) {
    return open(p, flags, mode);
}
static inline int sceIoRead(SceUID fd, void *p, unsigned size) {
    return (int)read(fd, p, size);
}
static inline SceOff sceIoLseek(SceUID fd, SceOff off, int whence) {
    return (SceOff)lseek(fd, (off_t)off, whence);
}
static inline int sceIoWrite(SceUID fd, const void *p, unsigned size) {
    return (int)write(fd, p, size);
}
static inline int sceIoClose(SceUID fd) { return close(fd); }
#endif
