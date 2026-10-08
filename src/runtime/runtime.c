#include "runtime.h"
#include "data.h"
#include "log.h"
int dspsp_runtime_init(void){ dspsp_log_init(); dspsp_log("runtime init"); int r=dspsp_data_init(); dspsp_log("runtime data status=%d",r); dspsp_log("published root present=%d",dspsp_data_exists("published")); dspsp_log("core ini present=%d",dspsp_data_exists("EAMCore.ini")); return 0; }
void dspsp_runtime_shutdown(void){ dspsp_log("runtime shutdown"); dspsp_log_close(); }
