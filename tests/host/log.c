#include <stdarg.h>
#include <stdio.h>
void dspsp_log(const char *fmt, ...) {
    va_list args; va_start(args, fmt); vfprintf(stderr, fmt, args);
    fputc('\n', stderr); va_end(args);
}

int dspsp_log_write(const void *data, unsigned size) {
    size_t n = fwrite(data, 1, size, stderr);
    return n == 0 && size && ferror(stderr) ? -1 : (int)n;
}
