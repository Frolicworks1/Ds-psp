#include <stdarg.h>
#include <stdio.h>
void dspsp_log(const char *fmt, ...) {
    va_list args; va_start(args, fmt); vfprintf(stderr, fmt, args);
    fputc('\n', stderr); va_end(args);
}
