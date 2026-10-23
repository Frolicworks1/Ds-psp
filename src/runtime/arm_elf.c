#include "arm_elf.h"
#include <pspiofilemgr.h>
#include <elf.h>
#include <string.h>

static int read_at(SceUID fd, SceOff off, void *buf, unsigned int size) {
    if (sceIoLseek(fd, off, PSP_SEEK_SET) < 0) return -1;
    return sceIoRead(fd, buf, size) == (int)size ? 0 : -1;
}

int ds_arm_elf_probe(const char *path, DsArmElfInfo *info) {
    if (!path || !info) return -1;
    memset(info, 0, sizeof(*info));

    SceUID fd = sceIoOpen(path, PSP_O_RDONLY, 0);
    if (fd < 0) return -2;

    Elf32_Ehdr eh;
    if (read_at(fd, 0, &eh, sizeof(eh)) < 0 ||
        memcmp(eh.e_ident, ELFMAG, SELFMAG) != 0 ||
        eh.e_ident[EI_CLASS] != ELFCLASS32 ||
        eh.e_ident[EI_DATA] != ELFDATA2LSB ||
        eh.e_machine != EM_ARM) {
        sceIoClose(fd);
        return -3;
    }

    uint32_t lo = 0xffffffffu, hi = 0;
    uint32_t loads = 0;
    unsigned i;
    for (i = 0; i < eh.e_phnum; ++i) {
        Elf32_Phdr ph;
        if (read_at(fd, (SceOff)eh.e_phoff + (SceOff)i * eh.e_phentsize,
                    &ph, sizeof(ph)) < 0) {
            sceIoClose(fd);
            return -4;
        }
        if (ph.p_type != PT_LOAD) continue;
        ++loads;
        if (ph.p_vaddr < lo) lo = ph.p_vaddr;
        if (ph.p_vaddr + ph.p_memsz > hi) hi = ph.p_vaddr + ph.p_memsz;
    }

    sceIoClose(fd);
    if (!loads || lo == 0xffffffffu || hi <= lo) return -5;

    info->entry = eh.e_entry;
    info->image_base = lo;
    info->image_size = hi - lo;
    info->load_segments = loads;
    return 0;
}
