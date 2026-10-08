#include "imports.h"
#include "../cpu/armv6.h"
#include "arm_loader.h"
#include "log.h"
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

#define DS_IMPORT_MAX 512

static const char *g_names[DS_IMPORT_MAX];
static unsigned g_count;
static DsArmImage *g_image;

static uint32_t trampoline_for(unsigned id) {
    return DS_IMPORT_TRAMP_BASE + id * 4u;
}

void ds_imports_init(void) {
    memset(g_names, 0, sizeof(g_names));
    g_count = 0;
    g_image = 0;
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

void ds_imports_bind_image(DsArmImage *image) { g_image = image; }

static void *guest_ptr(uint32_t addr) {
    if (!g_image || addr < g_image->image_base) return 0;
    uint32_t off = addr - g_image->image_base;
    if (off >= g_image->image_size + g_image->heap_size) return 0;
    return g_image->image + off;
}

static uint32_t guest_alloc(uint32_t size) {
    if (!g_image || !size) return 0;
    size = (size + 7u) & ~7u;
    if (size > g_image->heap_size - g_image->heap_used) return 0;
    uint32_t p = g_image->heap_base + g_image->heap_used;
    g_image->heap_used += size;
    return p;
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
        /* Guest heap is arena-backed; individual free is intentionally deferred. */
        cpu->r[0] = 0;
    } else if (!strcmp(name, "memset")) {
        cpu->r[0] = (uint32_t)(uintptr_t)memset(
            guest_ptr(cpu->r[0]), (int)cpu->r[1], cpu->r[2]);
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
        cpu->r[0] = (uint32_t)strlen((const char *)guest_ptr(cpu->r[0]));
    } else if (!strcmp(name, "strcmp")) {
        cpu->r[0] = (uint32_t)strcmp(
            (const char *)(uintptr_t)cpu->r[0],
            (const char *)guest_ptr(cpu->r[1]));
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
