#include "donor_elf.h"
#include <pspiofilemgr.h>
#include <stdio.h>
#include <string.h>
#include <elf.h>

int dspsp_inspect_donor(const char *path, char *report, unsigned int report_size) {
    if (!report || report_size == 0) return -1;
    report[0] = 0;

    SceUID fd = sceIoOpen(path, PSP_O_RDONLY, 0);
    if (fd < 0) {
        snprintf(report, report_size, "donor: missing (%s)", path);
        return -2;
    }

    unsigned char ident[EI_NIDENT];
    int n = sceIoRead(fd, ident, sizeof(ident));
    sceIoClose(fd);

    if (n != EI_NIDENT || memcmp(ident, ELFMAG, SELFMAG) != 0) {
        snprintf(report, report_size, "donor: not ELF");
        return -3;
    }

    if (ident[EI_CLASS] != ELFCLASS32 || ident[EI_DATA] != ELFDATA2LSB) {
        snprintf(report, report_size, "donor: unsupported ELF format");
        return -4;
    }

    /* ARM32 ELF uses e_machine == EM_ARM (40). The file is inspected only;
       no ARM code is executed by the PSP process. */
    snprintf(report, report_size, "donor: ARM32 little-endian ELF");
    return 0;
}
