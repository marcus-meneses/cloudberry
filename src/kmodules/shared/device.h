#ifndef CLOUDBERRY_DEVICE_H
#define CLOUDBERRY_DEVICE_H
#include <stdint.h>
#include <stddef.h>
/* Kernel device identity, separate from future application handles. IDs are
 * stable for this boot; generation-qualified references never expose interpreter
 * pointers. Descriptions are not resource claims or permission grants. */
enum device_bus {DEVICE_BUS_PCI=1,DEVICE_BUS_PLATFORM,DEVICE_BUS_FIRMWARE_CHILD,DEVICE_BUS_USB};
enum device_flags {DEVICE_PRESENT=1,DEVICE_ENABLED=2,DEVICE_FUNCTIONING=4,DEVICE_RESOURCE_ERROR=8,DEVICE_ASSOCIATION_ERROR=16};
struct device_acpi_ref {uint64_t id,generation;};
struct device_identity {uint64_t id,generation,parent_id,owner;uint32_t bus,flags;struct device_acpi_ref acpi;};
enum device_resource_kind {DEVICE_MEMORY=1,DEVICE_IO,DEVICE_IRQ,DEVICE_DMA,DEVICE_BUS_RANGE,DEVICE_SERIAL,DEVICE_GPIO,DEVICE_OTHER};
enum device_resource_flags {DEVICE_RESOURCE_PRODUCER=1,DEVICE_RESOURCE_SHARED=2,DEVICE_RESOURCE_LEVEL=4,DEVICE_RESOURCE_LOW=8,DEVICE_RESOURCE_READONLY=16,DEVICE_RESOURCE_UNRESOLVED=32};
struct device_resource {uint32_t kind,flags,firmware_type,index;uint64_t minimum,maximum,length,translation,alignment;const char *source;};
#define DEVICE_PCI_ID(index) (UINT64_C(0x1000000000000000)|((index)+1))
#define DEVICE_PLATFORM_ID(id) (UINT64_C(0x2000000000000000)|(id))
#endif
