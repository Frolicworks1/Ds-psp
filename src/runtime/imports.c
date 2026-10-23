#include "imports.h"
#include "../cpu/armv6.h"
#include "arm_loader.h"
#include "log.h"
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

#define DS_IMPORT_MAX 512

static char g_names[DS_IMPORT_MAX][256];
static unsigned g_count;
static DsArmImage *g_image;
static uint32_t g_ctype_objects[3];
static uint32_t g_stdio;
static uint32_t g_globals[3];

static uint32_t trampoline_for(unsigned id) {
    return DS_IMPORT_TRAMP_BASE + id * 4u;
}

void ds_imports_init(void) {
    memset(g_names, 0, sizeof(g_names));
    g_count = 0;
    g_image = 0;
    g_stdio = 0;
    memset(g_globals, 0, sizeof(g_globals));
    memset(g_ctype_objects, 0, sizeof(g_ctype_objects));
}

void *ds_import_resolve(const char *library, const char *symbol) {
    (void)library;
    if (!symbol || !*symbol || strlen(symbol) >= sizeof(g_names[0])) return 0;

    for (unsigned i = 0; i < g_count; ++i)
        if (!strcmp(g_names[i], symbol))
            return (void *)(uintptr_t)trampoline_for(i);

    if (g_count >= DS_IMPORT_MAX) return 0;

    strcpy(g_names[g_count], symbol);
    ++g_count;
    return (void *)(uintptr_t)trampoline_for(g_count - 1);
}

void ds_imports_bind_image(DsArmImage *image) {
    if (g_image != image) {
        memset(g_ctype_objects, 0, sizeof(g_ctype_objects));
        g_stdio = 0;
        memset(g_globals, 0, sizeof(g_globals));
    }
    g_image = image;
}

static void *guest_range(uint32_t addr, uint32_t size) {
    if (!g_image || addr < g_image->image_base) return 0;
    uint32_t off = addr - g_image->image_base;
    uint64_t total = (uint64_t)g_image->image_size + g_image->heap_size;
    if (off > total || size > total - off) return 0;
    return g_image->image + off;
}

static const char *guest_string(uint32_t addr) {
    const char *p = guest_range(addr, 1);
    if (!p) return 0;
    size_t remaining = (size_t)g_image->image_size + g_image->heap_size -
                       (addr - g_image->image_base);
    return memchr(p, 0, remaining) ? p : 0;
}

static uint32_t guest_alloc(uint32_t size) {
    if (!g_image || !size || size > UINT32_MAX - 7u ||
        g_image->heap_used > g_image->heap_size) return 0;
    size = (size + 7u) & ~7u;
    if (size > g_image->heap_size - g_image->heap_used) return 0;
    uint32_t p = g_image->heap_base + g_image->heap_used;
    if (p < g_image->heap_base || size > UINT32_MAX - p || !guest_range(p, size)) return 0;
    g_image->heap_used += size;
    return p;
}

static void store32(uint8_t *p, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) p[i] = (uint8_t)(value >> (8 * i));
}

static uint32_t load32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
           (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

/* Single guest execution thread (id 1). No host pthread objects are used.
 * Shared/contended operations stop until a guest scheduler exists. Return
 * Android errno values to the guest; negative values are interpreter stops. */
static int mutex_call(const char *name, struct DsArmv6Cpu *cpu) {
    static const char *supported[] = {
        "pthread_mutexattr_init", "pthread_mutexattr_destroy",
        "pthread_mutexattr_gettype", "pthread_mutexattr_settype",
        "pthread_mutexattr_getpshared", "pthread_mutexattr_setpshared",
        "pthread_mutex_init", "pthread_mutex_destroy", "pthread_mutex_lock",
        "pthread_mutex_trylock", "pthread_mutex_unlock"
    };
    unsigned i;
    for (i = 0; i < sizeof(supported) / sizeof(supported[0]); ++i)
        if (!strcmp(name, supported[i])) break;
    if (i == sizeof(supported) / sizeof(supported[0])) return -32;
    uint32_t addr = cpu->r[0], arg = cpu->r[1];
    if (!addr) { cpu->r[0] = 22; return 0; } /* EINVAL */
    uint8_t *p = guest_range(addr, 4);
    if (!p || (addr & 3)) return -31;
    uint32_t value = load32(p), result = 0;
    if (!strcmp(name, "pthread_mutexattr_init")) store32(p, 0);
    else if (!strcmp(name, "pthread_mutexattr_destroy")) store32(p, UINT32_MAX);
    else if (!strcmp(name, "pthread_mutexattr_settype")) {
        if (arg > 2) result = 22;
        else store32(p, (value & ~15u) | arg);
    } else if (!strcmp(name, "pthread_mutexattr_gettype") ||
               !strcmp(name, "pthread_mutexattr_getpshared")) {
        uint8_t *out = guest_range(arg, 4);
        if (!arg) result = 22;
        else if (!out || (arg & 3)) return -31;
        else if ((value & 15) > 2) result = 22;
        else store32(out, !strcmp(name, "pthread_mutexattr_gettype") ? value & 15 : !!(value & 16));
    } else if (!strcmp(name, "pthread_mutexattr_setpshared")) {
        if (arg > 1) result = 22;
        else store32(p, (value & ~16u) | (arg << 4));
    } else if (!strcmp(name, "pthread_mutex_init")) {
        uint32_t attr = 0;
        if (arg) {
            const uint8_t *a = guest_range(arg, 4);
            if (!a || (arg & 3)) return -31;
            attr = load32(a);
        }
        if ((attr & 15) > 2) result = 22;
        else if (attr & 16) return -32;
        else store32(p, (attr & 15) << 14);
    } else if (!strcmp(name, "pthread_mutex_lock") ||
               !strcmp(name, "pthread_mutex_trylock") ||
               !strcmp(name, "pthread_mutex_unlock") ||
               !strcmp(name, "pthread_mutex_destroy")) {
        unsigned type = (value >> 14) & 3, state = value & 3;
        unsigned owner = value >> 16;
        if (value == 0xdead10ccu || type == 3 || state == 3) result = 22;
        else if (value & 0x2000 || state == 2) return -32;
        else if ((!type && (value & ~3u)) ||
                 (!state && (value & 0xffff1ffcu)) ||
                 (type && state && !owner)) result = 22;
        else if (!strcmp(name, "pthread_mutex_destroy")) {
            if (state) result = 16; /* EBUSY */
            else store32(p, 0xdead10ccu);
        } else if (!strcmp(name, "pthread_mutex_unlock")) {
            if (type && (!state || owner != 1)) result = 1; /* EPERM */
            else if (type == 1 && (value & 0x1ffc)) store32(p, value - 4);
            else store32(p, type << 14);
        } else if (!state) {
            store32(p, (type << 14) | 1u | (type ? 0x10000u : 0));
        } else if (type && owner == 1) {
            if (type == 2) result = 35; /* Bionic EDEADLK */
            else if ((value & 0x1ffc) == 0x1ffc) result = 11; /* EAGAIN, no wrap */
            else store32(p, value + 4);
        } else if (!strcmp(name, "pthread_mutex_trylock")) result = 16;
        else return -32; /* Cannot wait without scheduling another guest thread. */
    } else return -32;
    cpu->r[0] = result;
    return 0;
}

/* Three 84-byte ARM FILEs followed by three 12-byte extensions. No host
 * FILE or callback pointers are exposed. Wide I/O is disabled in this ABI. */
static uint32_t stdio_data(void) {
    if (g_stdio) return g_stdio;
    unsigned saved_count = g_count;
    uint32_t callbacks[4];
    const char *names[] = {"__sclose", "__sread", "__sseek", "__swrite"};
    for (unsigned i = 0; i < 4; ++i) {
        callbacks[i] = (uint32_t)(uintptr_t)ds_import_resolve(0, names[i]);
        if (!callbacks[i]) { g_count = saved_count; return 0; }
    }
    uint32_t addr = guest_alloc(288);
    if (!addr) { g_count = saved_count; return 0; }
    uint8_t *p = guest_range(addr, 288);
    memset(p, 0, 288);
    for (unsigned i = 0; i < 3; ++i) {
        uint8_t *f = p + i * 84;
        f[12] = i == 0 ? 4 : i == 1 ? 8 : 10;
        f[14] = (uint8_t)i;
        store32(f + 28, addr + i * 84); /* cookie */
        for (unsigned j = 0; j < 4; ++j) store32(f + 32 + j * 4, callbacks[j]);
        store32(f + 48, addr + 252 + i * 12); /* extension */
    }
    g_stdio = addr;
    return addr;
}

static int standard_stream(uint32_t addr) {
    if (!g_stdio || addr < g_stdio || addr - g_stdio >= 252 ||
        (addr - g_stdio) % 84) return -1;
    return (int)((addr - g_stdio) / 84);
}

/* Output goes directly to the port log, with no buffered bytes to flush.
 * -31 is a bad guest range, -32 an unsupported stream; -1 is a sink error. */
static int stream_write(uint32_t stream, uint32_t addr, uint32_t size) {
    int index = standard_stream(stream);
    if (index < 1) return -32;
    const void *p = guest_range(addr, size);
    if (!p || size > INT32_MAX) return -31;
    int written = dspsp_log_write(p, size);
    if (written < 0 || (uint32_t)written < size) {
        uint8_t *f = guest_range(stream, 84);
        f[12] |= 0x40; /* __SERR */
    }
    return written < 0 ? -1 : written;
}

/* Android's legacy C-locale tables: slot 0 is EOF, slots 1..256 are
 * unsigned bytes. Symbols hold 32-bit pointers to those tables. See
 * docs/BIONIC_ABI.md for the source contract and high-byte classification. */
uint32_t ds_import_resolve_data(const char *symbol) {
    if (!symbol || !g_image) return 0;
    if (!strcmp(symbol, "__sF")) return stdio_data();
    int global = !strcmp(symbol, "__page_size") ? 0 :
                 !strcmp(symbol, "__stack_chk_guard") ? 1 :
                 !strcmp(symbol, "tzname") ? 2 : -1;
    if (global >= 0) {
        if (g_globals[global]) return g_globals[global];
        uint32_t addr = guest_alloc(global == 2 ? 12 : 4);
        if (!addr) return 0;
        uint8_t *p = guest_range(addr, global == 2 ? 12 : 4);
        if (global == 0) store32(p, 4096);
        /* Bionic's documented fallback when random canary bytes are unavailable. */
        else if (global == 1) store32(p, 0xff0a0000u);
        else {
            store32(p, addr + 8); store32(p + 4, addr + 8);
            memcpy(p + 8, "   ", 4); /* WILDABBR, before tzset */
        }
        g_globals[global] = addr;
        return addr;
    }
    unsigned kind;
    if (!strcmp(symbol, "_ctype_")) kind = 0;
    else if (!strcmp(symbol, "_tolower_tab_")) kind = 1;
    else if (!strcmp(symbol, "_toupper_tab_")) kind = 2;
    else return 0;
    if (g_ctype_objects[kind]) return g_ctype_objects[kind];
    unsigned width = kind ? 2 : 1;
    uint32_t object = guest_alloc(4 + 257 * width);
    if (!object) return 0;
    uint8_t *p = guest_range(object, 4 + 257 * width);
    uint32_t table = object + 4;
    for (unsigned b = 0; b < 4; ++b) p[b] = (uint8_t)(table >> (8 * b));
    p += 4;
    p[0] = kind ? 0xff : 0;
    if (kind) p[1] = 0xff; /* signed short EOF = -1 */
    for (unsigned c = 0; c < 256; ++c) {
        unsigned value = c;
        if (kind == 1 && c >= 'A' && c <= 'Z') value += 'a' - 'A';
        if (kind == 2 && c >= 'a' && c <= 'z') value -= 'a' - 'A';
        if (!kind) {
            if (c < 32 || (c >= 127 && c < 160)) value = 0x20;
            else if (c == ' ') value = 0x88;
            else if (c >= '0' && c <= '9') value = 0x04;
            else if (c >= 'A' && c <= 'Z') value = 0x01;
            else if (c >= 'a' && c <= 'z') value = 0x02;
            else value = 0x10;
            if (c >= 9 && c <= 13) value |= 0x08;
            if ((c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')) value |= 0x40;
        }
        p[(c + 1) * width] = (uint8_t)value;
        if (kind) p[(c + 1) * width + 1] = (uint8_t)(value >> 8);
    }
    g_ctype_objects[kind] = object;
    return object;
}

unsigned ds_import_resolve_count(void) {
    return g_count;
}

int ds_import_is_trampoline(uint32_t guest_addr) {
    return guest_addr >= DS_IMPORT_TRAMP_BASE &&
           guest_addr < DS_IMPORT_TRAMP_BASE + g_count * 4u &&
           ((guest_addr - DS_IMPORT_TRAMP_BASE) & 3u) == 0;
}

static const char *name_for(uint32_t guest_addr) {
    if (!ds_import_is_trampoline(guest_addr)) return 0;
    unsigned id = (guest_addr - DS_IMPORT_TRAMP_BASE) / 4u;
    return id < g_count ? g_names[id] : 0;
}

/*
 * ARM EABI arguments arrive in r0-r3.  This bridge deliberately implements
 * only operations whose ABI is unambiguous in the guest.  Unsupported calls
 * stop execution with an error; they must never masquerade as successful calls.
 */
int ds_import_call(uint32_t guest_addr, struct DsArmv6Cpu *cpu) {
    const char *name = name_for(guest_addr);
    if (!name || !cpu) return -1;

    if (!strcmp(name, "__stack_chk_fail")) {
        dspsp_log("guest stack corruption detected pc=0x%08x", cpu->pc);
        return -33;
    }
    if (!strncmp(name, "pthread_mutex", 13)) {
        int rc = mutex_call(name, cpu);
        if (rc) {
            dspsp_log("guest mutex stop: %s error=%d pc=0x%08x", name, rc, cpu->pc);
            return rc;
        }
    } else if (!strcmp(name, "fwrite") || !strcmp(name, "fputs") || !strcmp(name, "__swrite")) {
        uint32_t stream, addr, count, unit = 1;
        int is_fwrite = !strcmp(name, "fwrite");
        int is_fputs = !strcmp(name, "fputs");
        if (is_fwrite) {
            addr = cpu->r[0]; unit = cpu->r[1]; count = cpu->r[2]; stream = cpu->r[3];
            if (unit && count > INT32_MAX / unit) return -31;
            count *= unit;
        } else if (is_fputs) {
            addr = cpu->r[0]; stream = cpu->r[1];
            const char *str = guest_string(addr);
            if (!str) return -31;
            count = (uint32_t)strlen(str);
        } else {
            stream = cpu->r[0]; addr = cpu->r[1]; count = cpu->r[2];
        }
        if (!is_fwrite && standard_stream(stream) < 1) return -32;
        int result = count ? stream_write(stream, addr, count) : 0;
        if (result == -31 || result == -32) return result;
        cpu->r[0] = is_fwrite ? (result < 0 || !unit ? 0 : (uint32_t)result / unit) :
                    is_fputs ? (result < 0 || (uint32_t)result != count ? UINT32_MAX : 0) : (uint32_t)result;
    } else if (!strcmp(name, "fflush") || !strcmp(name, "fileno") ||
               !strcmp(name, "ferror") || !strcmp(name, "clearerr")) {
        int index = standard_stream(cpu->r[0]);
        if (index < 0) return -32;
        uint8_t *f = guest_range(cpu->r[0], 84);
        if (!strcmp(name, "fileno")) cpu->r[0] = (uint32_t)index;
        else if (!strcmp(name, "ferror")) cpu->r[0] = !!(f[12] & 0x40);
        else {
            if (!strcmp(name, "clearerr")) f[12] &= (uint8_t)~0x60;
            cpu->r[0] = 0;
        }
    } else if (!strcmp(name, "malloc")) {
        cpu->r[0] = guest_alloc(cpu->r[0]);
    } else if (!strcmp(name, "calloc")) {
        uint32_t n = cpu->r[0], size = cpu->r[1];
        if (size && n > 0xffffffffu / size) cpu->r[0] = 0;
        else cpu->r[0] = guest_alloc(n * size);
        if (cpu->r[0]) memset(guest_range(cpu->r[0], n * size), 0, n * size);
    } else if (!strcmp(name, "free")) {
        cpu->r[0] = 0;
    } else if (!strcmp(name, "memset") || !strcmp(name, "memcpy") ||
               !strcmp(name, "memmove") || !strcmp(name, "memcmp")) {
        uint32_t n = cpu->r[2];
        void *dst = guest_range(cpu->r[0], n);
        if (!dst) return -31;
        if (!strcmp(name, "memset")) memset(dst, (int)cpu->r[1], n);
        else {
            void *src = guest_range(cpu->r[1], n);
            if (!src) return -31;
            if (!strcmp(name, "memcmp")) cpu->r[0] = (uint32_t)memcmp(dst, src, n);
            else if (!strcmp(name, "memmove")) memmove(dst, src, n);
            else {
                uint64_t d = cpu->r[0], s = cpu->r[1];
                if (n && d < s + n && s < d + n) return -31;
                memcpy(dst, src, n);
            }
        }
    } else if (!strcmp(name, "strlen") || !strcmp(name, "strcmp")) {
        const char *a = guest_string(cpu->r[0]);
        if (!a) return -31;
        if (!strcmp(name, "strlen")) cpu->r[0] = (uint32_t)strlen(a);
        else {
            const char *b = guest_string(cpu->r[1]);
            if (!b) return -31;
            cpu->r[0] = (uint32_t)strcmp(a, b);
        }
    } else if (!strcmp(name, "__android_log_print")) {
        cpu->r[0] = 0;
    } else {
        dspsp_log("guest import unsupported: %s pc=0x%08x", name, cpu->pc);
        return -32;
    }

    cpu->pc = cpu->r[14] & ~1u;
    cpu->thumb = cpu->r[14] & 1u;
    return 0;
}
