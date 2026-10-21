/* Failed HID topology workers must notify their provider without waiting again. */
#include "../kmodules/shared/hid-reports.c"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static struct {struct kernel_surfaces hub;struct kernel_hub_table table;} fixture;
static struct usb_table devices;
static struct hid_link subscription;
static unsigned posts,watches;
static struct kernel_surface_result cpu_stub(MODULE_ARGS){MODULE_UNUSED();return module_result(0,1);}
static struct kernel_surface_result info_stub(MODULE_ARGS){MODULE_UNUSED();*(struct process_info *)b=(struct process_info){.id=a};return module_result(1,1);}
static struct kernel_surface_result thread_stub(MODULE_ARGS){MODULE_UNUSED();if(a==THREAD_WAKE)return module_result(1,1);assert(a==THREAD_BLOCK && posts==1);return module_result(0,0);}
static struct kernel_surface_result watch_stub(MODULE_ARGS){MODULE_UNUSED();assert(a<=1);++watches;return module_result(1,1);}
static struct kernel_surface_result clock_stub(MODULE_ARGS){MODULE_UNUSED();return module_result(123,1);}
static struct kernel_surface_result post_stub(MODULE_ARGS){MODULE_UNUSED();assert(a==1 && b==7 && d==sizeof(struct cb_io_hid_event));const struct cb_io_hid_event *event=(const void *)c;assert(event->subscription==7);if(!posts)assert(event->kind==CB_IO_HID_CHANGED);else assert(event->kind==CB_IO_ERROR && event->status==CB_PROVIDER_FAILED);++posts;return module_result(1,1);}
int main(void){
    fixture.hub.tables_offset=sizeof(fixture.hub);fixture.hub.table_count=1;strcpy(fixture.table.name,USB_TABLE_NAME);fixture.table.address=(uintptr_t)&devices;
    subscription=(struct hid_link){.pid=1,.id=7,.watcher=1};hid.hub=&fixture.hub;hid.links=&subscription;hid.cpu=cpu_stub;hid.threads=thread_stub;hid.watch=watch_stub;hid.clock=clock_stub;hid.post=post_stub;
    struct kernel_surface_result result=hid_surface_report_worker((uintptr_t)&subscription,0,0,0,0,0);assert(result.status && !result.value && posts==2 && watches==2 && subscription.ready==1 && subscription.failed);
    hid.keyboard_pid=1;hid.process_info=info_stub;struct cb_io_subscription ack={7};struct io_invocation invocation={.process=1,.operation=CB_IO_HID_ACK,.input=&ack,.bytes=sizeof(ack)};assert(hid_reports_invoke(&invocation) && invocation.status==CB_PROVIDER_FAILED);
    subscription.stop=1;posts=watches=0;result=hid_surface_report_worker((uintptr_t)&subscription,0,0,0,0,0);assert(!result.status && result.value && !posts && watches==2);
    puts("PASS HID worker failure: terminal provider notification without another wait; orderly stop emits no false failure");return 0;
}
