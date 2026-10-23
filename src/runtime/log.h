#ifndef DSPSP_LOG_H
#define DSPSP_LOG_H
void dspsp_log_init(void);
void dspsp_log(const char *fmt, ...);
/* Raw guest console output: bytes written, or -1 on sink failure. */
int dspsp_log_write(const void *data, unsigned size);
void dspsp_log_close(void);
#endif
