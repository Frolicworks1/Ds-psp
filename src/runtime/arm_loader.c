#include "arm_loader.h"
#include <pspiofilemgr.h>
#include <elf.h>
#include <stdlib.h>
#include <string.h>

static int read_at(SceUID fd, SceOff off, void *buf, unsigned int size) {
    if (sceIoLseek(fd, off, PSP_SEEK_SET) < 0) return -1;
    return sceIoRead(fd, buf, size) == (int)size ? 0 : -1;
}

void ds_arm_loader_unload(DsArmImage *image) {
    if (!image) return;
    free(image->image);
    memset(image, 0, sizeof(*image));
}

const uint8_t *ds_arm_loader_ptr(const DsArmImage *image, uint32_t vaddr) {
    if (!image || !image->image || vaddr < image->image_base ||
        vaddr - image->image_base >= image->image_size + image->heap_size) return 0;
    return image->image + (vaddr - image->image_base);
}

int ds_arm_loader_load(const char *path, DsArmImage *out) {
    return ds_arm_loader_load_with_mmap(path, out, 2u * 1024u * 1024u);
}

int ds_arm_loader_load_with_mmap(const char *path, DsArmImage *out, uint32_t mmap_bytes) {
    if (!path || !out) return -1;
    memset(out, 0, sizeof(*out));
    if (!mmap_bytes || mmap_bytes > DS_ARM_MAX_MMAP_BYTES || (mmap_bytes & 4095u)) return -10;

    SceUID fd = sceIoOpen(path, PSP_O_RDONLY, 0);
    if (fd < 0) return -2;

    Elf32_Ehdr eh;
    if (read_at(fd, 0, &eh, sizeof(eh)) < 0 ||
        memcmp(eh.e_ident, ELFMAG, SELFMAG) != 0 ||
        eh.e_ident[EI_CLASS] != ELFCLASS32 ||
        eh.e_ident[EI_DATA] != ELFDATA2LSB ||
        eh.e_machine != EM_ARM || eh.e_version != EV_CURRENT ||
        eh.e_ident[EI_VERSION] != EV_CURRENT ||
        (eh.e_type != ET_DYN && eh.e_type != ET_EXEC) ||
        eh.e_ehsize != sizeof(eh) || eh.e_phentsize != sizeof(Elf32_Phdr)) {
        sceIoClose(fd);
        return -3;
    }

    uint32_t lo = 0xffffffffu, hi = 0, loads = 0;
    for (unsigned i = 0; i < eh.e_phnum; ++i) {
        Elf32_Phdr ph;
        if (read_at(fd, (SceOff)eh.e_phoff + (SceOff)i * eh.e_phentsize,
                    &ph, sizeof(ph)) < 0) {
            sceIoClose(fd); return -4;
        }
        if (ph.p_type != PT_LOAD) continue;
        if (ph.p_filesz > ph.p_memsz || ph.p_memsz > UINT32_MAX - ph.p_vaddr) {
            sceIoClose(fd); return -5;
        }
        ++loads;
        if (ph.p_vaddr < lo) lo = ph.p_vaddr;
        if (ph.p_vaddr + ph.p_memsz > hi) hi = ph.p_vaddr + ph.p_memsz;
    }

    if (!loads || lo == 0xffffffffu || hi <= lo) {
        sceIoClose(fd); return -5;
    }

    /* Page-align the arena so the guest allocator receives an aligned break. */
    if (hi > UINT32_MAX - 4095u) { sceIoClose(fd); return -6; }
    hi = (hi + 4095u) & ~4095u;
    uint32_t size = hi - lo;
    const uint32_t heap_size = 6u * 1024u * 1024u + mmap_bytes;
    if (hi > UINT32_MAX - heap_size) { sceIoClose(fd); return -6; }
    uint8_t *mem = (uint8_t *)malloc(size + heap_size);
    if (!mem) {
        sceIoClose(fd); return -6;
    }
    memset(mem, 0, size + heap_size);

    for (unsigned i = 0; i < eh.e_phnum; ++i) {
        Elf32_Phdr ph;
        if (read_at(fd, (SceOff)eh.e_phoff + (SceOff)i * eh.e_phentsize,
                    &ph, sizeof(ph)) < 0) {
            free(mem); sceIoClose(fd); return -7;
        }
        if (ph.p_type != PT_LOAD) continue;
        if (ph.p_filesz > ph.p_memsz || ph.p_vaddr < lo ||
            ph.p_vaddr - lo > size ||
            ph.p_filesz > size - (ph.p_vaddr - lo)) {
            free(mem); sceIoClose(fd); return -8;
        }
        if (ph.p_filesz &&
            read_at(fd, ph.p_offset, mem + (ph.p_vaddr - lo), ph.p_filesz) < 0) {
            free(mem); sceIoClose(fd); return -9;
        }
    }

    sceIoClose(fd);
    out->image = mem;
    out->image_base = lo;
    out->image_size = size;
    out->entry = eh.e_entry;
    out->load_segments = loads;
    out->heap_base = lo + size;
    out->heap_size = heap_size;
    out->heap_used = 0;
    out->bridge_size = 1024u * 1024u;
    out->brk_base = out->heap_base + out->bridge_size;
    out->brk_size = 4u * 1024u * 1024u;
    out->mmap_base = out->brk_base + out->brk_size;
    out->mmap_size = mmap_bytes;
    out->stack_base = out->mmap_base + out->mmap_size;
    out->stack_size = 1024u * 1024u;
    return 0;
}
