/* A failed capture worker must notify its provider before it exits. */
#include "../kmodules/shared/io.c"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static struct {struct kernel_surfaces hub;struct kernel_hub_table table;} fixture;
static struct usb_table devices;
static struct subscription subscription;
static unsigned posts,watches;
static struct kernel_surface_result cpu_stub(MODULE_ARGS){MODULE_UNUSED();return module_result(0,1);}
static struct kernel_surface_result thread_stub(MODULE_ARGS){MODULE_UNUSED();assert(a==THREAD_BLOCK && !posts);return module_result(0,0);}
static struct kernel_surface_result watch_stub(MODULE_ARGS){MODULE_UNUSED();assert(a<=1);++watches;return module_result(1,1);}
static struct kernel_surface_result clock_stub(MODULE_ARGS){MODULE_UNUSED();return module_result(123,1);}
static struct kernel_surface_result post_stub(MODULE_ARGS){MODULE_UNUSED();assert(a==1 && b==7 && d==sizeof(struct cb_io_keyboard_event));const struct cb_io_keyboard_event *event=(const void *)c;assert(event->kind==CB_IO_ERROR && event->status==CB_PROVIDER_FAILED && event->subscription==7);++posts;return module_result(1,1);}
int main(void){
    fixture.hub.tables_offset=sizeof(fixture.hub);fixture.hub.table_count=1;strcpy(fixture.table.name,USB_TABLE_NAME);fixture.table.address=(uintptr_t)&devices;
    subscription=(struct subscription){.pid=1,.id=7};state.hub=&fixture.hub;state.first=&subscription;state.cpu=cpu_stub;state.thread=thread_stub;state.watch=watch_stub;state.clock=clock_stub;state.post=post_stub;
    struct kernel_surface_result result=io_surface_worker((uintptr_t)&subscription,0,0,0,0,0);
    assert(result.status && !result.value && posts==1 && watches==2 && subscription.ready==1 && !subscription.waiting);
    subscription.stop=1;posts=watches=0;result=io_surface_worker((uintptr_t)&subscription,0,0,0,0,0);
    assert(!result.status && result.value && !posts && watches==2);
    puts("PASS keyboard IO failure: failed worker posts terminal provider failure; orderly stop emits no false failure");return 0;
}
