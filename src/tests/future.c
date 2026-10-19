/* Disk-only acceptance module. Never present in candidate images or Limine. */
#include "../kmodules/shared/module-load.h"
#include "../kmodules/shared/hwenum.h"
static struct module_init_context copied;
static uint64_t hardware;static unsigned ready;
struct kernel_surface_result future_init(MODULE_ARGS){
    MODULE_UNUSED();const struct module_init_context *ctx=(const void *)a;if(!ctx || ctx->abi!=2 || !ctx->hub || !ctx->heap || ctx->device_count!=1 || !ctx->devices)return module_result(1,0);
    const struct kernel_hub_owner *owner=kernel_hub_owner(ctx->hub,ctx->owner);
    if(!owner || owner->state!=HUB_OWNER_INITIALIZING || kernel_surface_find(ctx->hub,"future.value") || !kernel_surface_find_version(ctx->hub,"clock.now_ns",1,0))return module_result(2,0);
    const struct kernel_hub_table *hw=kernel_hub_find_table(ctx->hub,HWENUM_TABLE_NAME);if(!hw || ctx->devices[0]>=((const struct hwenum_table *)hw->address)->count)return module_result(3,0);
    hardware=ctx->devices[0];if(((const struct hwenum_table *)hw->address)->entries[hardware].data.pci.class_code!=3)return module_result(4,0);
    copied=*ctx;copied.devices=NULL;return module_result(1,1);
}
struct kernel_surface_result future_start(MODULE_ARGS){MODULE_UNUSED();const struct module_init_context *ctx=(const void *)a;if(ctx->owner!=copied.owner || kernel_hub_owner(ctx->hub,ctx->owner)->state!=HUB_OWNER_STARTING || kernel_surface_find(ctx->hub,"future.value") || !ctx->resolve || !ctx->resolve(ctx->owner,"future.value",1,2) || ctx->resolve(ctx->owner,"future.value",1,3))return module_result(0,0);
    /* Even a captured mediation stub must reject calls throughout startup. */
    const struct kernel_surface *entries=kernel_surface_entries(ctx->hub);
    for(uint32_t i=0;i<kernel_surface_count(ctx->hub);++i)if(entries[i].owner==ctx->owner && !entries[i].call(0,0,0,0,0,0).status)return module_result(0,0);
    if(!kernel_surface_find(ctx->hub,"hub.owner_pin")(ctx->owner,1,0,0,0,0).status)return module_result(0,0);
    ready=1;return module_result(1,1);}
struct kernel_surface_result future_value(MODULE_ARGS){MODULE_UNUSED();return module_result(hardware+42,ready);}

#include "../kmodules/shared/bootstrap-load.h"
#include "../kmodules/shared/block-util.h"
#define CALL(fn,a,b,c) (fn)((uintptr_t)(a),(uintptr_t)(b),(uintptr_t)(c),0,0,0)
struct kernel_surface_result future_test(MODULE_ARGS){
    MODULE_UNUSED();const struct kernel_surfaces *hub=copied.hub;unsigned line=0;void *image=NULL;
    kernel_surface_call alloc=kernel_surface_find(hub,"mem.calloc"),free=kernel_surface_find(hub,"mem.free"),stat=kernel_surface_find(hub,"ext4_fs.stat"),read=kernel_surface_find(hub,"ext4_fs.read"),load=kernel_surface_find(hub,"kernel.module_load"),owner=kernel_surface_find(hub,"hub.owner_control");
#define CHECK(x) do{if(!(x)){line=__LINE__;goto done;}}while(0)
    struct bootstrap_info info;CHECK(!CALL(kernel_surface_find(hub,"bootstrap.info"),&info,0,0).status);
    const char *path="/boot/kmodules/drivers/future-fail.kmod";struct ext4_stat st;CHECK(!CALL(stat,info.filesystem.cookie,path,&st).status);
    struct kernel_surface_result memory=CALL(alloc,copied.heap,1,st.bytes);CHECK(!memory.status);image=(void *)memory.value;
    CHECK(!read(info.filesystem.cookie,(uintptr_t)path,0,(uintptr_t)image,st.bytes,0).status);
    struct module_requirement wrong={.name="future.value",.major=1,.minor=3};struct module_load_request request={.abi=1,.image=image,.bytes=st.bytes,.name="future-copy",.init="future_fail.init",.requirements=&wrong,.requirement_count=1};struct module_load_result result;
    uint32_t before=kernel_surface_count(hub);CHECK(CALL(load,&request,&result,0).status && result.error==MODULE_LOAD_DEPENDENCY && result.owner==UINT64_MAX && kernel_surface_count(hub)==before);
    request.requirement_count=0;struct kmodule_image *header=image;uint32_t format=header->format;header->format=2;
    CHECK(CALL(load,&request,&result,0).status && result.error==MODULE_LOAD_IMAGE && result.owner==UINT64_MAX && kernel_surface_count(hub)==before);header->format=format;
    /* A second unique image exercises post-publication start failure. */
    const char *replacement="future-startfail";for(unsigned i=0;i<32;++i){header->name[i]=0;request.name[i]=0;}disk_copy(header->name,replacement,16);disk_copy(request.name,replacement,16);
    struct kmodule_export *exports=(void *)((uintptr_t)image+header->exports_offset);
    for(uint32_t i=0;i<header->exports_count;++i){const char *name=kernel_hub_name_equal(exports[i].name,"future_fail.init")?"future_startfail.init":kernel_hub_name_equal(exports[i].name,"future_fail.start")?"future_startfail.start":"future_startfail.value";for(unsigned j=0;j<32;++j)exports[i].name[j]=0;size_t bytes=0;while(name[bytes])++bytes;disk_copy(exports[i].name,name,bytes);}
    for(unsigned i=0;i<32;++i)request.init[i]=0;
    disk_copy(request.init,"future_startfail.init",21);disk_copy(request.start,"future_startfail.start",22);request.devices=&hardware;request.device_count=1;
    CHECK(CALL(load,&request,&result,0).status && result.error==MODULE_LOAD_START && result.detail==88 && result.state==HUB_OWNER_FAILED);
    CHECK(!kernel_surface_find(hub,"future_startfail.value") && !CALL(owner,result.owner,HUB_OWNER_RETIRE,0).status);
    const struct kernel_hub_table *ref=kernel_hub_find_table(hub,"bootstrap.drivers");CHECK(ref);const struct driver_table *drivers=(const void *)ref->address;const struct driver_record *failed=drivers->first;
    while(failed && !kernel_hub_name_equal(failed->name,"future-fail"))failed=failed->next;
    CHECK(failed && failed->result.error==MODULE_LOAD_INIT && failed->result.detail==77 && !kernel_surface_find(hub,"future_fail.value"));
    CHECK(kernel_hub_owner(hub,copied.owner)->pins>=2); /* Test thread + dependency. */
    CHECK(!CALL(owner,failed->result.owner,HUB_OWNER_RETIRE,0).status && kernel_hub_owner(hub,copied.owner)->pins==1);
    /* A captured failed entry still goes through the rejecting mediation gate. */
    const struct kernel_surface *methods=kernel_surface_entries(hub);for(uint32_t i=0;i<kernel_surface_count(hub);++i)if(kernel_hub_name_equal(methods[i].name,"future_fail.value"))CHECK(methods[i].call(0,0,0,0,0,0).status);
done:if(image)CALL(free,copied.heap,image,0);return module_result(line,!line);
#undef CHECK
}
