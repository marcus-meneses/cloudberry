#ifndef CLOUDBERRY_USB_STORAGE_H
#define CLOUDBERRY_USB_STORAGE_H
#include "usb.h"
#include "block.h"
enum usb_storage_error {STORAGE_OK,STORAGE_ARGUMENT,STORAGE_MEMORY,STORAGE_BUSY,STORAGE_UNSUPPORTED,STORAGE_DISCONNECTED,STORAGE_TRANSPORT,STORAGE_PROTOCOL,STORAGE_COMMAND,STORAGE_RANGE};
struct usb_storage_open {struct usb_ref device;uint32_t interface,lun;};
struct usb_storage_record {
    struct usb_storage_record *next;struct block_device block;struct usb_ref device;
    uint32_t interface,lun,peripheral,error;uint64_t reads,commands;
    uint8_t sense_key,asc,ascq,failed;
};
struct usb_storage_table {uint64_t count;struct usb_storage_record *first;};
/* init(hub,heap); open(request,out block,capacity); info(cookie,out record);
 * close(cookie), read/write/flush/control follow block.h. Explicit interface/LUN selection;
 * SCSI transparent BOT (08/06/50), direct-access read/write and CD/DVD read-only devices.
 * Failed open can return a cleanup-only out.cookie; retry close if nonzero.
 * One LUN per claimed interface at a time. No automatic discovery/claim/retries.
 * Command failure captures REQUEST SENSE. Invalid wrappers/transport failure
 * poison the instance; USB rebind may be needed before reopen. */
struct kernel_surface_result usb_storage_surface_init(MODULE_ARGS);
struct kernel_surface_result usb_storage_surface_open(MODULE_ARGS);
struct kernel_surface_result usb_storage_surface_read(MODULE_ARGS);
struct kernel_surface_result usb_storage_surface_control(MODULE_ARGS);
struct kernel_surface_result usb_storage_surface_info(MODULE_ARGS);
struct kernel_surface_result usb_storage_surface_close(MODULE_ARGS);
struct kernel_surface_result usb_storage_surface_write(MODULE_ARGS);
struct kernel_surface_result usb_storage_surface_flush(MODULE_ARGS);
#endif
