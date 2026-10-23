#ifndef DSPSP_DATA_H
#define DSPSP_DATA_H
int dspsp_data_init(void);
int dspsp_data_exists(const char *relative);
const char *dspsp_data_path(const char *relative);
#endif
