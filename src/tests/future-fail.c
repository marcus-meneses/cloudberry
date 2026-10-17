#include "../kmodules/shared/module-load.h"
struct kernel_surface_result future_fail_init(MODULE_ARGS){MODULE_UNUSED();const struct module_init_context *ctx=(const void *)a;if(kernel_surface_find(ctx->hub,"future_fail.value"))return module_result(99,0);return module_result(ctx->device_count?1:77,ctx->device_count!=0);}
struct kernel_surface_result future_fail_value(MODULE_ARGS){MODULE_UNUSED();return module_result(99,1);}

struct kernel_surface_result future_fail_start(MODULE_ARGS){MODULE_UNUSED();const struct module_init_context *ctx=(const void *)a;
    if(kernel_hub_owner(ctx->hub,ctx->owner)->state!=HUB_OWNER_STARTING)return module_result(89,0);
    const struct kernel_surface *entries=kernel_surface_entries(ctx->hub);
    for(uint32_t i=0;i<kernel_surface_count(ctx->hub);++i)if(entries[i].owner==ctx->owner && !entries[i].call(0,0,0,0,0,0).status)return module_result(90,0);
    return module_result(88,0);}
