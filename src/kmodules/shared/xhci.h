#ifndef CLOUDBERRY_XHCI_H
#define CLOUDBERRY_XHCI_H
#include "usb-host.h"
#define XHCI_TABLE_NAME "xhci.controllers"
#define XHCI_TABLE_MAGIC UINT64_C(0x3149434858424343)
enum xhci_state {XHCI_OPENING=1,XHCI_ONLINE,XHCI_QUIESCING,XHCI_FAILED,XHCI_CLOSED};
struct xhci_info {uint64_t id,hardware_index,generation,interrupts,events,route_id;uint32_t state,ports,slots,context_bytes,scratchpads,version,stage,reserved;};
struct xhci_port {uint64_t generation,changes;uint32_t number,status,protocol_major,protocol_minor,slot_type,speed_id_count;uint32_t speed_ids[15];};
struct xhci_record {struct xhci_record *next;struct xhci_info info;struct xhci_port *ports;};
struct xhci_table {uint64_t magic;uint16_t major,minor;uint32_t record_bytes;uint64_t count;struct xhci_record *first;};
/* BSP foreground, trusted pointers. init(hub,heap) publishes a live linked
 * registry; does not claim hardware. open(hardware_index,out info) -> ID.
 * info(id,out), port(id,1-based port,out). Raw registry views require caller
 * synchronization; snapshot methods copy under the CPU lock.
 * reset_port(id,port) handles USB2 PR / USB3 WPR without enumeration.
 * device(id,host_device_request,out token) enables/addresses a root or downstream slot.
 * endpoint(id,host_endpoint_request) configures bulk/interrupt endpoints;
 * endpoint address is the USB endpoint address (0x81 = EP1 IN). Address 0
 * with HOST_CONTROL evaluates the existing EP0 maximum packet size.
 * transfer(id,host_transfer_request) -> asynchronous request ID, <=4096 bytes.
 * result(id,request_id,out,reap) copies IN bytes into the retained caller buffer
 * only when reaping a completed request; that buffer must remain valid until reap.
 * wait(id,request_id,out,absolute_deadline_ns) blocks with wake credits; timeout
 * leaves the request owned and pending until completion/cancel. One waiter per
 * request. UINT64_MAX means notification-only waiting, without a timer deadline.
 * Reap before closing; close refuses outstanding results/waiters.
 * cancel(id,device,endpoint) stops/resets endpoint and advances its dequeue.
 * hub(id,request) configures hub/TT context. disconnect(id,token) invalidates
 * the subtree and wakes pending I/O without releasing DMA.
 * remove(id,device) disables a slot only after all children were removed; close(id) halts/resets controller and releases
 * ownership after draining callbacks. Failed quiescence retains all DMA storage.
 * Device tokens are fresh per binding; port_generation rejects stale attachment.
 * open/close/reset/device/endpoint/cancel/remove require a preemptible BSP thread with IF=1, outside critical
 * sections and IRQ callbacks. Command mutations use bounded wake-driven
 * serialization; transfers may coexist on different endpoints. No streams,
 * isochronous transfers, power management or image unloading in this layer yet. */
/* watch(id,current_thread_id) registers the root-bus owner; watch(id,0) clears
 * its subscription. Port changes/controller failures credit a thread wake from
 * deferred context. Unsubscribe before close. No polling worker is introduced. */
struct kernel_surface_result xhci_surface_hub(MODULE_ARGS);
struct kernel_surface_result xhci_surface_disconnect(MODULE_ARGS);
struct kernel_surface_result xhci_surface_watch(MODULE_ARGS);
struct kernel_surface_result xhci_surface_init(MODULE_ARGS);
struct kernel_surface_result xhci_surface_open(MODULE_ARGS);
struct kernel_surface_result xhci_surface_info(MODULE_ARGS);
struct kernel_surface_result xhci_surface_port(MODULE_ARGS);
struct kernel_surface_result xhci_surface_reset_port(MODULE_ARGS);
struct kernel_surface_result xhci_surface_device(MODULE_ARGS);
struct kernel_surface_result xhci_surface_endpoint(MODULE_ARGS);
struct kernel_surface_result xhci_surface_transfer(MODULE_ARGS);
struct kernel_surface_result xhci_surface_result(MODULE_ARGS);
struct kernel_surface_result xhci_surface_wait(MODULE_ARGS);
struct kernel_surface_result xhci_surface_cancel(MODULE_ARGS);
struct kernel_surface_result xhci_surface_remove(MODULE_ARGS);
struct kernel_surface_result xhci_surface_close(MODULE_ARGS);
struct kernel_surface_result xhci_surface_interrupt(MODULE_ARGS);
#endif
