#include "imports.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>

static unsigned g_resolved;

static uint32_t ds_log_stub(void) { return 0; }

void ds_imports_init(void) { g_resolved = 0; }

void *ds_import_resolve(const char *library, const char *symbol) {
    if (!symbol) return 0;

    /*
     * Keep the resolver explicit.  Game-specific JNI/GLES functions are not
     * guessed here; they will be added behind the PSP compatibility layer.
     * These common libc entry points are safe host-side PSP equivalents.
     */
    if (!strcmp(symbol, "malloc")) { ++g_resolved; return (void *)(uintptr_t)malloc; }
    if (!strcmp(symbol, "calloc")) { ++g_resolved; return (void *)(uintptr_t)calloc; }
    if (!strcmp(symbol, "realloc")) { ++g_resolved; return (void *)(uintptr_t)realloc; }
    if (!strcmp(symbol, "free")) { ++g_resolved; return (void *)(uintptr_t)free; }
    if (!strcmp(symbol, "memcpy")) { ++g_resolved; return (void *)(uintptr_t)memcpy; }
    if (!strcmp(symbol, "memmove")) { ++g_resolved; return (void *)(uintptr_t)memmove; }
    if (!strcmp(symbol, "memset")) { ++g_resolved; return (void *)(uintptr_t)memset; }
    if (!strcmp(symbol, "memcmp")) { ++g_resolved; return (void *)(uintptr_t)memcmp; }
    if (!strcmp(symbol, "strlen")) { ++g_resolved; return (void *)(uintptr_t)strlen; }
    if (!strcmp(symbol, "strcmp")) { ++g_resolved; return (void *)(uintptr_t)strcmp; }
    if (!strcmp(symbol, "__android_log_print")) { ++g_resolved; return (void *)(uintptr_t)ds_log_stub; }

    (void)library;
    return 0;
}

unsigned ds_import_resolve_count(void) { return g_resolved; }
