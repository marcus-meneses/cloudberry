#ifndef CLOUDBERRY_BOOTSTRAP_H
#define CLOUDBERRY_BOOTSTRAP_H
#include "block.h"
#include "ext4-fs.h"
#include "usb.h"
enum bootstrap_state {BOOTSTRAP_IDLE,BOOTSTRAP_DISCOVERING,BOOTSTRAP_READY,BOOTSTRAP_FAILED,BOOTSTRAP_DISCONNECTED};
enum bootstrap_error {BOOTSTRAP_OK,BOOTSTRAP_CONFIG,BOOTSTRAP_MEMORY,BOOTSTRAP_TRANSPORT,BOOTSTRAP_GPT,BOOTSTRAP_NOT_FOUND,BOOTSTRAP_DUPLICATE,BOOTSTRAP_FILESYSTEM,BOOTSTRAP_TOPOLOGY,BOOTSTRAP_TIMEOUT};
enum bootstrap_transport {BOOTSTRAP_USB=1,BOOTSTRAP_NVME,BOOTSTRAP_SATA};
enum bootstrap_origin {BOOTSTRAP_ORIGIN_UNKNOWN,BOOTSTRAP_ORIGIN_MATCH,BOOTSTRAP_ORIGIN_DIFFERENT};
struct bootstrap_info {
    uint32_t state,error,transport,origin;
    uint64_t examined,matches,hardware_index,usb_controller;uint32_t namespace_id,port;
    struct usb_ref usb;uint8_t disk_guid[16],partition_guid[16];
    struct block_device disk,partition;struct ext4_info filesystem;
};
/* init(hub,heap) publishes bootstrap.volume; start() launches one CPU0 worker.
 * select() is the synchronous worker operation, one initial selection only.
 * info(out) copies status. The selected disk/slice/mount are bootstrap-owned;
 * consumers use the filesystem cookie and must not close these objects.
 * System mount follows filesystem.writable and transport capabilities; writable
 * ext4 may replay its journal. No formatting/migration/automatic replacement.
 * GUID selection and boot-origin correlation are distinct, explicit results. */
struct kernel_surface_result bootstrap_surface_init(MODULE_ARGS);
struct kernel_surface_result bootstrap_surface_start(MODULE_ARGS);
struct kernel_surface_result bootstrap_surface_select(MODULE_ARGS);
struct kernel_surface_result bootstrap_surface_info(MODULE_ARGS);
#endif
