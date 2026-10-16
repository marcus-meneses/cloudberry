#ifndef CLOUDBERRY_USB_HOST_H
#define CLOUDBERRY_USB_HOST_H
#include "module.h"
/* Version 1 host-controller payload contract. No USB enumeration policy here.
 * Trusted kernel buffers; no exposed DMA addresses or xHCI contexts/TRBs. */
enum host_endpoint_type {HOST_CONTROL=1,HOST_BULK,HOST_INTERRUPT};
enum host_transfer_state {HOST_PENDING=1,HOST_COMPLETE,HOST_CANCELLED,HOST_DISCONNECTED,HOST_FAILED};
struct host_device_request {uint32_t port,max_packet;uint64_t port_generation;};
struct host_endpoint_request {uint64_t device;uint32_t address,type,max_packet,interval,max_burst,max_esit;};
struct host_transfer_request {uint64_t device;uint32_t endpoint,bytes;void *buffer;uint8_t setup[8];};
struct host_transfer_result {uint64_t id;uint32_t state,completion_code,bytes,reserved;};
#endif
