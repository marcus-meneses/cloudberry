#ifndef CLOUDBERRY_USB_H
#define CLOUDBERRY_USB_H
#include "module.h"
#include "device.h"
#include "usb-host.h"
#define USB_TABLE_NAME "usb.devices"
#define USB_TABLE_MAGIC UINT64_C(0x3142535542424343)
#define DEVICE_USB_ID(id) (UINT64_C(0x3000000000000000)|(id))
enum usb_speed {USB_LOW=1,USB_FULL,USB_HIGH,USB_SUPER};
enum usb_state {USB_ENUMERATING=1,USB_CONFIGURED,USB_DETACHED,USB_FAILED,USB_UNSUPPORTED};
enum usb_error {USB_OK,USB_MEMORY,USB_TRANSPORT,USB_DESCRIPTOR,USB_TIMEOUT,USB_UNSUPPORTED_SPEED,USB_HUB_DEFERRED,USB_TRANSPORT_LIMIT,USB_QUIESCENCE};
struct usb_ref {uint64_t id,generation;};
struct usb_device {
    struct usb_device *next;
    struct device_identity identity;
    uint64_t controller,port_generation;
    uint32_t port,speed,state,error,root_port,depth;
    uint16_t vendor,product,usb_version,device_version;
    uint8_t class_code,subclass,protocol,configurations,configuration,interfaces,manufacturer,product_string,serial_string,hub;
};
struct usb_table {uint64_t magic;uint16_t major,minor;uint32_t record_bytes;uint64_t count,changes;struct usb_device *first;};
struct usb_info {uint64_t controllers,running,failed,records,connected,configured,worker_wakes,changes;};
struct usb_interface {uint64_t owner;uint8_t number,alternate,class_code,subclass,protocol,string_index;uint16_t endpoints;};
struct usb_endpoint {uint8_t address,transfer_type,interval,max_burst;uint16_t max_packet,max_esit;uint32_t host_type,supported;};
struct usb_io {
    struct usb_ref device;
    /* Zero: default bounded control/bulk, notification-only interrupt-IN.
     * Finite values: caller's absolute deadline. UINT64_MAX: notification-only. */
    uint64_t owner,deadline_ns;
    uint32_t interface,endpoint,bytes,reserved;
    void *buffer;
    uint8_t setup[8];
};
struct usb_hub_config {struct usb_ref device;uint64_t owner;uint32_t interface,ports,multi_tt,think_time;};
struct usb_downstream {struct usb_ref parent;uint64_t owner,generation;uint32_t port,speed,connected,reserved;};
struct kernel_surface_result usb_surface_cancel(MODULE_ARGS);
struct kernel_surface_result usb_surface_hub_configure(MODULE_ARGS);
struct kernel_surface_result usb_surface_downstream(MODULE_ARGS);
/* init(hub,heap) publishes an empty owned live registry. start() creates pinned
 * BSP enumeration workers; stop() requests shutdown and reaps exited workers on
 * repeat calls. info(out) reports running workers and retained failures.
 * worker is a hub entry for threads, not a client operation.
 * device(ref,out), configuration(ref,index,buffer,capacity,out_size),
 * interface(ref,ordinal,out), endpoint(ref,interface,address,out) copy metadata.
 * configuration copies the complete raw descriptor; insufficient space changes
 * only out_size. Records persist/reuse per topology connector; descriptor storage
 * is private. Generation-qualified refs reject detach/rebind races.
 * claim(ref,interface,owner)/release(ref,interface,owner) own alternate zero.
 * control(io,out host_transfer_result)/transfer(io,out result) synchronously
 * block on host IRQ completion. Interface claims scope class/vendor control
 * requests and endpoint access; device-wide standard mutations remain core-owned.
 * No caller buffer is retained on return, including timeout/recovery failure.
 * Operations serialize per device, refuse overlap, require a preemptible BSP
 * thread with IF enabled, and must not run in IRQ/deferred IRQ/critical contexts.
 * Snapshot and start/stop calls are also allowed in BSP bootstrap/dispatcher.
 * Raw live table traversal requires CPU-lock synchronization. usb-hub supplies
 * external-hub policy; alternate selection, other class drivers and image unload
 * remain deferred. hub_configure(config) requires a claimed class-9 interface;
 * downstream(request) requires that hub owner, and serializes subtree changes.
 * Accepted attachment may leave a FAILED child record; inspect usb.device.
 * cancel(io) cancels only the claimed interface endpoint, waking its I/O caller. */
struct kernel_surface_result usb_surface_init(MODULE_ARGS);
struct kernel_surface_result usb_surface_start(MODULE_ARGS);
struct kernel_surface_result usb_surface_stop(MODULE_ARGS);
struct kernel_surface_result usb_surface_worker(MODULE_ARGS);
/* watch(enable): subscribe/unsubscribe current BSP thread to coalesced device
 * and worker-state wakes. Subscribe before snapshot; block then rescan. Remove
 * before thread teardown. info.changes/table.changes are monotonic (saturating). */
struct kernel_surface_result usb_surface_watch(MODULE_ARGS);
struct kernel_surface_result usb_surface_info(MODULE_ARGS);
struct kernel_surface_result usb_surface_device(MODULE_ARGS);
struct kernel_surface_result usb_surface_configuration(MODULE_ARGS);
struct kernel_surface_result usb_surface_interface(MODULE_ARGS);
struct kernel_surface_result usb_surface_endpoint(MODULE_ARGS);
struct kernel_surface_result usb_surface_claim(MODULE_ARGS);
struct kernel_surface_result usb_surface_release(MODULE_ARGS);
struct kernel_surface_result usb_surface_control(MODULE_ARGS);
struct kernel_surface_result usb_surface_transfer(MODULE_ARGS);
struct kernel_surface_result usb_surface_ready(MODULE_ARGS);
#endif
