#ifndef CLOUDBERRY_BOOT_ORIGIN_H
#define CLOUDBERRY_BOOT_ORIGIN_H
#include <stdint.h>
/* Copied Limine executable metadata, immutable until reboot. No firmware/file
 * pointers. Zero GUIDs or optical/network media can leave transport unknown. */
struct boot_origin {uint32_t present,media_type,partition_index,mbr_disk_id;uint8_t disk_guid[16],partition_guid[16],filesystem_uuid[16];};
#endif
