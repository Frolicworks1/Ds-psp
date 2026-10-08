#include "imports.h"
#include "../cpu/armv6.h"
#include "log.h"
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

#define DS_IMPORT_MAX 512

static const char *g_names[DS_IMPORT_MAX];
static unsigned g_count;

static uint32_t trampoline_for(unsigned id) {
    return DS_IMPORT_TRAMP_BASE + id * 4u;
}

void ds_imports_init(void) {
    memset(g_names, 0, sizeof(g_names));
    g_count = 0;
}

void *ds_import_resolve(const char *library, const char *symbol) {
    (void)library;
    if (!symbol) return 0;

    for (unsigned i = 0; i < g_count; ++i)
        if (!strcmp(g_names[i], symbol))
            return (void *)(uintptr_t)trampoline_for(i);

    if (g_count >= DS_IMPORT_MAX) return 0;

    g_names[g_count] = symbol;
    ++g_count;
    return (void *)(uintptr_t)trampoline_for(g_count - 1);
}

unsigned ds_import_resolve_count(void) {
    return g_count;
}

int ds_import_is_trampoline(uint32_t guest_addr) {
    return guest_addr >= DS_IMPORT_TRAMP_BASE &&
           guest_addr < DS_IMPORT_TRAMP_BASE + DS_IMPORT_MAX * 4u &&
           ((guest_addr - DS_IMPORT_TRAMP_BASE) & 3u) == 0;
}

static const char *name_for(uint32_t guest_addr) {
    unsigned id = (guest_addr - DS_IMPORT_TRAMP_BASE) / 4u;
    return id < g_count ? g_names[id] : 0;
}

/*
 * ARM EABI arguments arrive in r0-r3.  This bridge deliberately implements
 * only operations whose ABI is unambiguous in the guest.  Unsupported calls
 * return zero and are logged instead of jumping into a MIPS function pointer.
 */
int ds_import_call(uint32_t guest_addr, struct DsArmv6Cpu *cpu) {
    const char *name = name_for(guest_addr);
    if (!name || !cpu) return -1;

    if (!strcmp(name, "malloc")) {
        cpu->r[0] = (uint32_t)(uintptr_t)malloc(cpu->r[0]);
    } else if (!strcmp(name, "calloc")) {
        cpu->r[0] = (uint32_t)(uintptr_t)calloc(cpu->r[0], cpu->r[1]);
    } else if (!strcmp(name, "realloc")) {
        cpu->r[0] = (uint32_t)(uintptr_t)realloc(
            (void *)(uintptr_t)cpu->r[0], cpu->r[1]);
    } else if (!strcmp(name, "free")) {
        free((void *)(uintptr_t)cpu->r[0]);
        cpu->r[0] = 0;
    } else if (!strcmp(name, "memset")) {
        cpu->r[0] = (uint32_t)(uintptr_t)memset(
            (void *)(uintptr_t)cpu->r[0], (int)cpu->r[1], cpu->r[2]);
    } else if (!strcmp(name, "memcpy")) {
        cpu->r[0] = (uint32_t)(uintptr_t)memcpy(
            (void *)(uintptr_t)cpu->r[0],
            (const void *)(uintptr_t)cpu->r[1], cpu->r[2]);
    } else if (!strcmp(name, "memmove")) {
        cpu->r[0] = (uint32_t)(uintptr_t)memmove(
            (void *)(uintptr_t)cpu->r[0],
            (const void *)(uintptr_t)cpu->r[1], cpu->r[2]);
    } else if (!strcmp(name, "memcmp")) {
        cpu->r[0] = (uint32_t)memcmp(
            (const void *)(uintptr_t)cpu->r[0],
            (const void *)(uintptr_t)cpu->r[1], cpu->r[2]);
    } else if (!strcmp(name, "strlen")) {
        cpu->r[0] = (uint32_t)strlen((const char *)(uintptr_t)cpu->r[0]);
    } else if (!strcmp(name, "strcmp")) {
        cpu->r[0] = (uint32_t)strcmp(
            (const char *)(uintptr_t)cpu->r[0],
            (const char *)(uintptr_t)cpu->r[1]);
    } else if (!strcmp(name, "__android_log_print")) {
        cpu->r[0] = 0;
    } else {
        dspsp_log("guest import unsupported: %s", name);
        cpu->r[0] = 0;
    }

    cpu->pc = cpu->r[14] & ~1u;
    cpu->thumb = cpu->r[14] & 1u;
    return 0;
}
