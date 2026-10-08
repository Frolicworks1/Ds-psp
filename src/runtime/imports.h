#ifndef DSPSP_IMPORTS_H
#define DSPSP_IMPORTS_H

#include <stdint.h>

typedef uint32_t (*DsImportFn)(void);

void ds_imports_init(void);
void *ds_import_resolve(const char *library, const char *symbol);
unsigned ds_import_resolve_count(void);

#endif
