#ifndef CLOUDBERRY_USB_HUB_H
#define CLOUDBERRY_USB_HUB_H
#include "usb.h"
#define USB_HUB_TABLE_NAME "usb.hubs"
#define USB_HUB_TABLE_MAGIC UINT64_C(0x3142554842424343)
enum usb_hub_state {USB_HUB_STARTING=1,USB_HUB_RUNNING,USB_HUB_DETACHED,USB_HUB_FAILED};
struct usb_hub_record {struct usb_hub_record *next;struct usb_ref device;uint64_t changes;uint32_t state,error,ports,interface,endpoint,superspeed;};
struct usb_hub_table {uint64_t magic;uint16_t major,minor;uint32_t record_bytes;uint64_t count;struct usb_hub_record *first;};
struct usb_hub_info {uint64_t records,running,failed;uint32_t manager,reserved;};
/* init(hub,heap), start(), stop(), info(out). Manager discovers class-9 hubs via
 * USB watch; each hub owns one preemptible worker, interrupt pipe and dynamic
 * port state. Threads stop/reap cooperatively; metadata remains per connector.
 * manager/worker are internal registered thread entries. No application ABI. */
struct kernel_surface_result usb_hub_surface_init(MODULE_ARGS);
struct kernel_surface_result usb_hub_surface_start(MODULE_ARGS);
struct kernel_surface_result usb_hub_surface_stop(MODULE_ARGS);
struct kernel_surface_result usb_hub_surface_info(MODULE_ARGS);
struct kernel_surface_result usb_hub_surface_manager(MODULE_ARGS);
struct kernel_surface_result usb_hub_surface_worker(MODULE_ARGS);
#endif
