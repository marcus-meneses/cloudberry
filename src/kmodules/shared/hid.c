#include "hid-reports.h"
struct kernel_surface_result hid_surface_init(MODULE_ARGS){MODULE_UNUSED();return module_result(1,a && b && hid_reports_init((const void *)a,b));}
struct kernel_surface_result hid_surface_invoke(MODULE_ARGS){MODULE_UNUSED();struct io_invocation *r=(void *)a;if(!r)return module_result(0,0);r->status=CB_BAD_REQUEST;r->actual=0;if((r->bytes && !r->input) || (r->capacity && !r->output))return module_result(1,1);(void)hid_reports_invoke(r);return module_result(1,1);}
struct kernel_surface_result hid_surface_exit(MODULE_ARGS){MODULE_UNUSED();cb_status_t status=hid_reports_exit(a);return module_result(status,status==CB_OK);}
struct kernel_surface_result hid_surface_info(MODULE_ARGS){MODULE_UNUSED();return hid_reports_info(a,b);}
struct kernel_surface_result hid_surface_bind(MODULE_ARGS){MODULE_UNUSED();if(!a)return module_result(0,0);hid_reports_bind((const void *)a,b);return module_result(1,1);}
