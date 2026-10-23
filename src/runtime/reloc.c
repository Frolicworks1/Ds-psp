#include "reloc.h"
#include "imports.h"
#include "log.h"
#include <pspiofilemgr.h>
#include <elf.h>
#include <string.h>
#include <stdint.h>

/* Guest addresses equal ELF virtual addresses: the load bias is zero even
 * when the lowest PT_LOAD address (image_base) is nonzero. Host pointers are
 * never written into the guest image. Failure leaves an unusable partial image;
 * callers must discard it and must not execute it. */
typedef struct {
    SceUID fd;
    Elf32_Phdr ph[32];
    unsigned count;
    uint32_t symtab, strtab, strsz;
} LinkContext;

static int read_at(SceUID fd, SceOff off, void *buf, unsigned size) {
    return sceIoLseek(fd, off, PSP_SEEK_SET) < 0 ||
           sceIoRead(fd, buf, size) != (int)size ? -1 : 0;
}

static int read_virtual(LinkContext *c, uint64_t addr, void *buf, unsigned size) {
    if (addr > UINT32_MAX || size > (uint64_t)UINT32_MAX + 1 - addr) return -1;
    for (unsigned i = 0; i < c->count; ++i) {
        const Elf32_Phdr *p = &c->ph[i];
        if (p->p_type != PT_LOAD || addr < p->p_vaddr) continue;
        uint64_t off = addr - p->p_vaddr;
        if (off <= p->p_filesz && size <= p->p_filesz - off)
            return read_at(c->fd, (SceOff)p->p_offset + off, buf, size);
    }
    return -1;
}

static int symbol_value(LinkContext *c, uint32_t index, uint32_t *value) {
    if (!index) { *value = 0; return 0; }
    Elf32_Sym sym;
    if (!c->symtab || read_virtual(c, (uint64_t)c->symtab +
        (uint64_t)index * sizeof(sym), &sym, sizeof(sym))) return -1;
    if (sym.st_shndx != SHN_UNDEF) {
        if (sym.st_shndx >= SHN_LORESERVE && sym.st_shndx != SHN_ABS) return -1;
        *value = sym.st_value;
        return 0;
    }
    if (ELF32_ST_BIND(sym.st_info) == STB_WEAK) { *value = 0; return 0; }
    if (!c->strtab || sym.st_name >= c->strsz) return -1;
    char name[256];
    for (unsigned i = 0; i < sizeof(name) && i < c->strsz - sym.st_name; ++i) {
        if (read_virtual(c, (uint64_t)c->strtab + sym.st_name + i, &name[i], 1)) return -1;
        if (!name[i]) {
            if (ELF32_ST_TYPE(sym.st_info) != STT_FUNC &&
                ELF32_ST_TYPE(sym.st_info) != STT_NOTYPE) {
                dspsp_log("unsupported guest data import: %s", name);
                return -1;
            }
            void *target = ds_import_resolve(0, name);
            if (!target) return -1;
            *value = (uint32_t)(uintptr_t)target;
            return 0;
        }
    }
    return -1;
}

static int apply_table(LinkContext *c, DsArmImage *im, uint32_t addr, uint32_t size) {
    for (uint32_t off = 0; off < size; off += sizeof(Elf32_Rel)) {
        Elf32_Rel rel;
        if (read_virtual(c, (uint64_t)addr + off, &rel, sizeof(rel))) return -7;
        unsigned type = ELF32_R_TYPE(rel.r_info), index = ELF32_R_SYM(rel.r_info);
        if (type == R_ARM_NONE) continue;
        if (rel.r_offset < im->image_base || im->image_size < 4 ||
            rel.r_offset - im->image_base > im->image_size - 4 || (rel.r_offset & 3)) return -8;
        uint8_t *where = im->image + rel.r_offset - im->image_base;
        uint32_t addend, value;
        memcpy(&addend, where, 4);
        if (type == R_ARM_RELATIVE) {
            if (index) return -9;
            value = addend; /* B + A; B is zero. */
        } else if (type == R_ARM_ABS32 || type == R_ARM_GLOB_DAT || type == R_ARM_JUMP_SLOT) {
            if (symbol_value(c, index, &value)) return -10;
            if (type == R_ARM_ABS32) value += addend;
        } else return -11;
        memcpy(where, &value, 4);
    }
    return 0;
}

int ds_arm_apply_relocations(const char *path, DsArmImage *image) {
    if (!path || !image || !image->image) return -1;
    LinkContext c = {0};
    c.fd = sceIoOpen(path, PSP_O_RDONLY, 0);
    if (c.fd < 0) return -2;
    int rc = -3;
    Elf32_Ehdr eh;
    if (read_at(c.fd, 0, &eh, sizeof(eh)) || memcmp(eh.e_ident, ELFMAG, SELFMAG) ||
        eh.e_ident[EI_CLASS] != ELFCLASS32 || eh.e_ident[EI_DATA] != ELFDATA2LSB ||
        eh.e_machine != EM_ARM || eh.e_phentsize != sizeof(Elf32_Phdr) ||
        eh.e_phnum > 32) goto done;
    c.count = eh.e_phnum;
    uint32_t rel = 0, relsz = 0, jmp = 0, jmpsz = 0, plt = 0;
    int dynamic = 0;
    for (unsigned i = 0; i < c.count; ++i) {
        if (read_at(c.fd, (SceOff)eh.e_phoff + i * sizeof(Elf32_Phdr), &c.ph[i], sizeof(c.ph[i]))) goto done;
        Elf32_Phdr *p = &c.ph[i];
        if (p->p_type != PT_DYNAMIC) continue;
        if (dynamic++ || p->p_filesz % sizeof(Elf32_Dyn)) goto done;
        int terminated = 0;
        for (uint32_t off = 0; off < p->p_filesz; off += sizeof(Elf32_Dyn)) {
            Elf32_Dyn d;
            if (read_at(c.fd, (SceOff)p->p_offset + off, &d, sizeof(d))) goto done;
            uint32_t v = d.d_un.d_val;
            if (d.d_tag == DT_NULL) { terminated = 1; break; }
            switch (d.d_tag) {
                case DT_REL: rel = v; break;
                case DT_RELSZ: relsz = v; break;
                case DT_RELENT: if (v != sizeof(Elf32_Rel)) goto done; break;
                case DT_JMPREL: jmp = v; break;
                case DT_PLTRELSZ: jmpsz = v; break;
                case DT_PLTREL: plt = v; break;
                case DT_SYMTAB: c.symtab = v; break;
                case DT_STRTAB: c.strtab = v; break;
                case DT_STRSZ: c.strsz = v; break;
                case DT_SYMENT: if (v != sizeof(Elf32_Sym)) goto done; break;
                case DT_RELASZ: if (v) goto done; break;
                default: break;
            }
        }
        if (!terminated) goto done;
    }
    if (relsz % sizeof(Elf32_Rel) || jmpsz % sizeof(Elf32_Rel) ||
        (jmpsz && plt != DT_REL)) goto done;
    rc = apply_table(&c, image, rel, relsz);
    if (!rc) rc = apply_table(&c, image, jmp, jmpsz);
done:
    sceIoClose(c.fd);
    return rc;
}
