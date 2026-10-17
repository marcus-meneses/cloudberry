#include "../kmodules/shared/module-load.h"
struct kernel_surface_result future_fail_init(MODULE_ARGS){MODULE_UNUSED();const struct module_init_context *ctx=(const void *)a;if(kernel_surface_find(ctx->hub,"future_fail.value"))return module_result(99,0);return module_result(ctx->device_count?1:77,ctx->device_count!=0);}
struct kernel_surface_result future_fail_value(MODULE_ARGS){MODULE_UNUSED();return module_result(99,1);}

struct kernel_surface_result future_fail_start(MODULE_ARGS){MODULE_UNUSED();return module_result(88,0);}
