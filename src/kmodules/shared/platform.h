#ifndef CLOUDBERRY_PLATFORM_H
#define CLOUDBERRY_PLATFORM_H
#include "module.h"
#include "device.h"
#define PLATFORM_TABLE_NAME "platform.devices"
#define PLATFORM_TABLE_MAGIC UINT64_C(0x31544c5041424343)
struct platform_device {struct device_identity identity;const char *path,*hid,*uid;uint32_t resource_count,cid_count;const struct device_resource *resources;const char *const *cids;};
struct platform_table {uint64_t magic;uint16_t major,minor;uint32_t entry_bytes;uint64_t count,generation;const struct platform_device *entries;};
/* init(hub,heap) publishes only firmware-described platform devices. PCI roots,
 * PCI companions, address-only unresolved bus children and I2C/SPI/etc children
 * are not independently claimed platform devices. Tables describe resources;
 * driver/resource ownership is not granted by enumeration. */
struct kernel_surface_result platform_surface_init(MODULE_ARGS);
struct kernel_surface_result platform_surface_device(MODULE_ARGS);
#endif
