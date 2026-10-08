#include "reloc.h"
#include "imports.h"
#include <pspiofilemgr.h>
#include <elf.h>
#include <string.h>
#include <stdint.h>

static int read_at(SceUID fd, SceOff off, void *buf, unsigned int size) {
    if (sceIoLseek(fd, off, PSP_SEEK_SET) < 0) return -1;
    return sceIoRead(fd, buf, size) == (int)size ? 0 : -1;
}

static SceOff vaddr_to_file(const Elf32_Phdr *ph, unsigned n, uint32_t v) {
    for (unsigned i = 0; i < n; ++i)
        if (ph[i].p_type == PT_LOAD && v >= ph[i].p_vaddr && v - ph[i].p_vaddr < ph[i].p_filesz)
            return (SceOff)ph[i].p_offset + (SceOff)(v - ph[i].p_vaddr);
    return (SceOff)-1;
}

static uint32_t *ptr32(DsArmImage *im, uint32_t v) {
    if (!im || !im->image || v < im->image_base ||
        v - im->image_base > im->image_size - sizeof(uint32_t)) return 0;
    return (uint32_t *)(void *)(im->image + v - im->image_base);
}

static uint32_t resolve_sym(const DsArmImage *im, uint32_t sym_value) {
    return im->image_base + (sym_value - im->image_base);
}

/*
 * Apply self-relative relocations that do not require Android/ELF imports.
 * External symbol resolution is deliberately left to the PSP compatibility
 * layer rather than guessing libc/GLES/JNI addresses.
 */
int ds_arm_apply_relocations(const char *path, DsArmImage *image) {
    ds_imports_init();
    if (!path || !image || !image->image) return -1;

    SceUID fd = sceIoOpen(path, PSP_O_RDONLY, 0);
    if (fd < 0) return -2;

    Elf32_Ehdr eh;
    if (read_at(fd, 0, &eh, sizeof(eh)) < 0) {
        sceIoClose(fd); return -3;
    }

    uint32_t rel_off=0, rel_sz=0, rel_ent=sizeof(Elf32_Rel);
    uint32_t jmprel_off=0, jmprel_sz=0, jmprel_type=0;
    uint32_t symtab=0, strtab=0, strsz=0, syment=sizeof(Elf32_Sym);
    Elf32_Phdr phdrs[32];
    unsigned phnum=eh.e_phnum>32?32:eh.e_phnum;
    int dynamic_found=0;

    for (unsigned i=0; i<phnum; ++i) {
        Elf32_Phdr ph;
        if (read_at(fd, (SceOff)eh.e_phoff + (SceOff)i * eh.e_phentsize,
                    &ph, sizeof(ph)) < 0) {
            sceIoClose(fd); return -4;
        }
        phdrs[i]=ph;
        if (ph.p_type != PT_DYNAMIC) continue;
        dynamic_found=1;
        unsigned count=ph.p_filesz / sizeof(Elf32_Dyn);
        for (unsigned j=0;j<count;++j) {
            Elf32_Dyn d;
            if (read_at(fd, ph.p_offset + (SceOff)j*sizeof(d), &d, sizeof(d)) < 0) {
                sceIoClose(fd); return -5;
            }
            if (d.d_tag == DT_REL) rel_off=d.d_un.d_ptr;
            else if (d.d_tag == DT_RELSZ) rel_sz=d.d_un.d_val;
            else if (d.d_tag == DT_RELENT && d.d_un.d_val) rel_ent=d.d_un.d_val;
            else if (d.d_tag == DT_JMPREL) jmprel_off=d.d_un.d_ptr;
            else if (d.d_tag == DT_PLTRELSZ) jmprel_sz=d.d_un.d_val;
            else if (d.d_tag == DT_PLTREL) jmprel_type=d.d_un.d_val;
            else if (d.d_tag == DT_SYMTAB) symtab=d.d_un.d_ptr;
            else if (d.d_tag == DT_STRTAB) strtab=d.d_un.d_ptr;
            else if (d.d_tag == DT_STRSZ) strsz=d.d_un.d_val;
            else if (d.d_tag == DT_SYMENT && d.d_un.d_val) syment=d.d_un.d_val;
        }
    }

    if (!dynamic_found) {
        sceIoClose(fd); return 0;
    }
    if (rel_ent != sizeof(Elf32_Rel) || (rel_sz && rel_sz % rel_ent) || (jmprel_sz && (jmprel_type != DT_REL || jmprel_sz % sizeof(Elf32_Rel)))) {
        sceIoClose(fd); return -6;
    }

    for (uint32_t off=0; off<rel_sz; off+=rel_ent) {
        Elf32_Rel rel;
        SceOff fo=vaddr_to_file(phdrs,phnum,rel_off+off);
        if (fo==(SceOff)-1 || read_at(fd,fo,&rel,sizeof(rel)) < 0) {
            sceIoClose(fd); return -7;
        }
        uint32_t type=ELF32_R_TYPE(rel.r_info);
        uint32_t sym_index=ELF32_R_SYM(rel.r_info);
        uint32_t *where=ptr32(image, rel.r_offset);
        if (!where) { sceIoClose(fd); return -8; }

        if (type == R_ARM_RELATIVE) {
            *where = image->image_base + *where;
        } else if (type == R_ARM_ABS32) {
            /*
             * For an already linked self-contained image this is enough to
             * rebase values that point inside the image. Imported symbols
             * are resolved later by the compatibility layer.
             */
            if (*where >= image->image_base &&
                *where < image->image_base + image->image_size)
                *where = resolve_sym(image, *where);
        } else if (type == R_ARM_JUMP_SLOT || type == R_ARM_GLOB_DAT) {
            /* External symbol lookup is intentionally deferred until the dynamic
             * symbol/string tables are parsed; never write a guessed address. */
            (void)sym_index;
        } else if (type == R_ARM_NONE) {
            continue;
        }
    }

    if (jmprel_off && jmprel_sz && symtab && strtab && syment == sizeof(Elf32_Sym)) {
        for (uint32_t off=0; off<jmprel_sz; off+=sizeof(Elf32_Rel)) {
            Elf32_Rel rel;
            SceOff fo=vaddr_to_file(phdrs,phnum,jmprel_off+off);
            if (fo==(SceOff)-1 || read_at(fd,fo,&rel,sizeof(rel))<0) continue;
            uint32_t idx=ELF32_R_SYM(rel.r_info);
            SceOff sf=vaddr_to_file(phdrs,phnum,symtab+idx*syment);
            Elf32_Sym sym;
            if (sf==(SceOff)-1 || read_at(fd,sf,&sym,sizeof(sym))<0 || sym.st_name>=strsz) continue;
            SceOff nf=vaddr_to_file(phdrs,phnum,strtab+sym.st_name);
            if (nf==(SceOff)-1) continue;
            char name[128]; unsigned k=0;
            for (; k+1<sizeof(name) && k+sym.st_name<strsz; ++k) {
                if (read_at(fd,nf+k,&name[k],1)<0) { k=0; break; }
                if (!name[k]) break;
            }
            if (k==0) continue;
            name[sizeof(name)-1]=0;
            void *target=ds_import_resolve(0,name);
            uint32_t *where=ptr32(image,rel.r_offset);
            if (target && where) *where=(uint32_t)(uintptr_t)target;
        }
    }
    sceIoClose(fd);
    return 0;
}
