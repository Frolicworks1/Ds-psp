#include "donor_elf.h"
#include <pspiofilemgr.h>
#include <stdio.h>
#include <string.h>
#include <elf.h>

static int read_at(SceUID fd, SceOff off, void *buf, unsigned int size) {
    if (sceIoLseek(fd, off, PSP_SEEK_SET) < 0) return -1;
    return sceIoRead(fd, buf, size) == (int)size ? 0 : -1;
}

static void appendf(char *out, unsigned int cap, unsigned int *used,
                    const char *fmt, unsigned int value) {
    if (*used >= cap) return;
    int n = snprintf(out + *used, cap - *used, fmt, value);
    if (n > 0) *used += (unsigned int)n;
}

int dspsp_inspect_donor(const char *path, char *report, unsigned int report_size) {
    if (!report || report_size == 0) return -1;
    report[0] = 0;

    SceUID fd = sceIoOpen(path, PSP_O_RDONLY, 0);
    if (fd < 0) {
        snprintf(report, report_size, "donor: missing (%s)", path);
        return -2;
    }

    Elf32_Ehdr eh;
    if (read_at(fd, 0, &eh, sizeof(eh)) < 0 ||
        memcmp(eh.e_ident, ELFMAG, SELFMAG) != 0) {
        sceIoClose(fd);
        snprintf(report, report_size, "donor: not ELF32");
        return -3;
    }

    if (eh.e_ident[EI_CLASS] != ELFCLASS32 ||
        eh.e_ident[EI_DATA] != ELFDATA2LSB) {
        sceIoClose(fd);
        snprintf(report, report_size, "donor: unsupported ELF format");
        return -4;
    }

    unsigned int used = 0;
    if (eh.e_machine != EM_ARM) {
        sceIoClose(fd);
        snprintf(report, report_size, "donor: unexpected machine=%u", (unsigned)eh.e_machine);
        return -5;
    }

    appendf(report, report_size, &used, "donor: ARM32 LE; type=%u phnum=%u",
            (unsigned)eh.e_type);
    appendf(report, report_size, &used, " shnum=%u", (unsigned)eh.e_shnum);

    unsigned int dyn_off = 0;
    unsigned int dyn_size = 0;
    unsigned int i;
    for (i = 0; i < eh.e_phnum; ++i) {
        Elf32_Phdr ph;
        if (read_at(fd, (SceOff)eh.e_phoff + (SceOff)i * eh.e_phentsize,
                    &ph, sizeof(ph)) < 0) {
            sceIoClose(fd);
            return -6;
        }
        if (ph.p_type == PT_DYNAMIC) {
            dyn_off = ph.p_offset;
            dyn_size = ph.p_filesz;
            break;
        }
    }

    unsigned int needed = 0, relsz = 0, pltrelsz = 0, symtab = 0, strtab = 0;
    if (dyn_off && dyn_size >= sizeof(Elf32_Dyn)) {
        unsigned int count = dyn_size / sizeof(Elf32_Dyn);
        for (i = 0; i < count; ++i) {
            Elf32_Dyn d;
            if (read_at(fd, (SceOff)dyn_off + (SceOff)i * sizeof(d),
                        &d, sizeof(d)) < 0) {
                sceIoClose(fd);
                return -7;
            }
            if (d.d_tag == DT_NULL) break;
            if (d.d_tag == DT_NEEDED) ++needed;
            else if (d.d_tag == DT_RELSZ) relsz = d.d_un.d_val;
            else if (d.d_tag == DT_PLTRELSZ) pltrelsz = d.d_un.d_val;
            else if (d.d_tag == DT_SYMTAB) symtab = d.d_un.d_ptr;
            else if (d.d_tag == DT_STRTAB) strtab = d.d_un.d_ptr;
        }
    }

    appendf(report, report_size, &used, " needed=%u relsz=%u pltrelsz=%u",
            needed, relsz, pltrelsz);
    appendf(report, report_size, &used, " symtab=0x%08x strtab=0x%08x",
            symtab, strtab);

    sceIoClose(fd);
    return 0;
}
