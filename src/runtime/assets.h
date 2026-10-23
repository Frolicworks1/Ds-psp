#ifndef DSPSP_ASSETS_H
#define DSPSP_ASSETS_H
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#define DS_ASSET_SLOTS 16
/* Trusted, extracted APK tree. Paths inside it must not contain symlinks.
 * Zero-initialize before first use. Reset closes streams and unmounts.
 * Read-only streaming: no whole-file allocation, no guest-visible FILE*. */
typedef struct {
    char root[512];
    FILE *files[DS_ASSET_SLOTS];
} DsAssets;
void ds_assets_reset(DsAssets *assets);
int ds_assets_mount(DsAssets *assets, const char *root);
int ds_assets_open(DsAssets *assets, const char *name); /* slot 1..16, -1 error */
int ds_assets_read(DsAssets *assets, unsigned slot, void *out, size_t size);
int ds_assets_skip(DsAssets *assets, unsigned slot, uint32_t bytes);
int ds_assets_close(DsAssets *assets, unsigned slot);
#endif
