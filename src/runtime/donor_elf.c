#include "donor_elf.h"
#include <pspiofilemgr.h>
#include <stdio.h>
#include <string.h>
#include <elf.h>

typedef struct {
    unsigned int vaddr;
    unsigned int offset;
    unsigned int filesz;
} LoadMap;

static int read_at(SceUID fd, SceOff off, void *buf, unsigned int size) {
    if (sceIoLseek(fd, off, PSP_SEEK_SET) < 0) return -1;
    return sceIoRead(fd, buf, size) == (int)size ? 0 : -1;
}

static int vaddr_to_file(const LoadMap *loads, unsigned int n,
                         unsigned int vaddr, unsigned int *file_off) {
    unsigned int i;
    for (i = 0; i < n; ++i) {
        if (vaddr >= loads[i].vaddr &&
            vaddr - loads[i].vaddr < loads[i].filesz) {
            *file_off = loads[i].offset + (vaddr - loads[i].vaddr);
            return 0;
        }
    }
    return -1;
}

static void appendf(char *out, unsigned int cap, unsigned int *used,
                    const char *fmt, unsigned int value) {
    if (*used >= cap) return;
    int n = snprintf(out + *used, cap - *used, fmt, value);
    if (n > 0) {
        unsigned int add = (unsigned int)n;
        *used += add < cap - *used ? add : cap - *used;
    }
}

static void appends(char *out, unsigned int cap, unsigned int *used,
                    const char *s) {
    if (*used >= cap) return;
    unsigned int left = cap - *used;
    int n = snprintf(out + *used, left, "%s", s);
    if (n > 0) {
        unsigned int add = (unsigned int)n;
        *used += add < left ? add : left;
    }
}

static int read_cstr(SceUID fd, unsigned int off, char *out, unsigned int cap) {
    unsigned int i;
    if (!cap) return -1;
    for (i = 0; i + 1 < cap; ++i) {
        unsigned char c;
        if (read_at(fd, (SceOff)off + i, &c, 1) < 0) return -1;
        out[i] = (char)c;
        if (!c) return 0;
    }
    out[cap - 1] = 0;
    return 0;
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
    if (eh.e_machine != EM_ARM) {
        sceIoClose(fd);
        snprintf(report, report_size, "donor: unexpected machine=%u",
                 (unsigned)eh.e_machine);
        return -5;
    }

    LoadMap loads[8];
    unsigned int load_count = 0, dyn_off = 0, dyn_size = 0, i;
    for (i = 0; i < eh.e_phnum; ++i) {
        Elf32_Phdr ph;
        if (read_at(fd, (SceOff)eh.e_phoff + (SceOff)i * eh.e_phentsize,
                    &ph, sizeof(ph)) < 0) {
            sceIoClose(fd);
            return -6;
        }
        if (ph.p_type == PT_LOAD && load_count < 8) {
            loads[load_count].vaddr = ph.p_vaddr;
            loads[load_count].offset = ph.p_offset;
            loads[load_count].filesz = ph.p_filesz;
            ++load_count;
        }
        if (ph.p_type == PT_DYNAMIC) {
            dyn_off = ph.p_offset;
            dyn_size = ph.p_filesz;
        }
    }

    unsigned int needed = 0, relsz = 0, pltrelsz = 0, relent = 0;
    unsigned int symtab = 0, strtab = 0, hash = 0, rel = 0, jmprel = 0;
    unsigned int strsz = 0, syment = sizeof(Elf32_Sym);
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
            switch (d.d_tag) {
            case DT_NEEDED: ++needed; break;
            case DT_RELSZ: relsz = d.d_un.d_val; break;
            case DT_PLTRELSZ: pltrelsz = d.d_un.d_val; break;
            case DT_RELENT: relent = d.d_un.d_val; break;
            case DT_SYMTAB: symtab = d.d_un.d_ptr; break;
            case DT_STRTAB: strtab = d.d_un.d_ptr; break;
            case DT_STRSZ: strsz = d.d_un.d_val; break;
            case DT_HASH: hash = d.d_un.d_ptr; break;
            case DT_REL: rel = d.d_un.d_ptr; break;
            case DT_JMPREL: jmprel = d.d_un.d_ptr; break;
            case DT_SYMENT: syment = d.d_un.d_val; break;
            default: break;
            }
        }
    }

    unsigned int used = 0;
    appendf(report, report_size, &used,
             "donor: ARM32 LE; type=%u phnum=%u shnum=%u",
             (unsigned)eh.e_type);
    appendf(report, report_size, &used, " loads=%u needed=%u",
            load_count);
    appendf(report, report_size, &used, " rel=%u plt=%u",
            relsz, pltrelsz);

    unsigned int sym_count = 0, hash_off = 0, str_off = 0, sym_off = 0;
    if (hash && vaddr_to_file(loads, load_count, hash, &hash_off) == 0) {
        unsigned int buckets = 0, chains = 0;
        if (read_at(fd, hash_off, &buckets, 4) == 0 &&
            read_at(fd, hash_off + 4, &chains, 4) == 0) {
            sym_count = chains;
        }
    }
    if (strtab) vaddr_to_file(loads, load_count, strtab, &str_off);
    if (symtab) vaddr_to_file(loads, load_count, symtab, &sym_off);

    appendf(report, report_size, &used, " syms=%u", sym_count);
    appendf(report, report_size, &used, " strsz=%u", strsz);
    appendf(report, report_size, &used, " relent=%u", relent);

    unsigned int imports = 0;
    char first_imports[96];
    first_imports[0] = 0;
    if (sym_count && sym_off && str_off && syment >= sizeof(Elf32_Sym)) {
        unsigned int limit = sym_count;
        if (limit > 4096) limit = 4096;
        for (i = 0; i < limit; ++i) {
            Elf32_Sym sym;
            if (read_at(fd, (SceOff)sym_off + (SceOff)i * syment,
                        &sym, sizeof(sym)) < 0) break;
            if (sym.st_shndx == SHN_UNDEF && sym.st_name < strsz) {
                ++imports;
                if (first_imports[0] == 0 && imports <= 4) {
                    char name[48];
                    if (read_cstr(fd, str_off + sym.st_name,
                                  name, sizeof(name)) == 0)
                        snprintf(first_imports, sizeof(first_imports),
                                 "%s", name);
                }
            }
        }
    }
    appendf(report, report_size, &used, " imports=%u", imports);
    if (first_imports[0]) {
        appends(report, report_size, &used, " first=");
        appends(report, report_size, &used, first_imports);
    }

    unsigned int rel_count = 0, plt_count = 0;
    if (rel && relsz && relent >= sizeof(Elf32_Rel)) rel_count = relsz / relent;
    if (jmprel && pltrelsz && relent >= sizeof(Elf32_Rel))
        plt_count = pltrelsz / relent;
    appendf(report, report_size, &used, " relocs=%u plt_relocs=%u",
            rel_count);
    (void)plt_count;

    sceIoClose(fd);
    return 0;
}
