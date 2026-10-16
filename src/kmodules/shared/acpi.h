#ifndef CLOUDBERRY_ACPI_H
#define CLOUDBERRY_ACPI_H
#include "module.h"
#include "device.h"
#define ACPI_TABLE_NAME "acpi.devices"
#define ACPI_TABLE_MAGIC UINT64_C(0x3149504341424343)
enum acpi_device_flags {ACPI_HAS_ADR=1,ACPI_HAS_SEG=2,ACPI_HAS_BBN=4,ACPI_PCI_ROOT=8,ACPI_IDENT_ERROR=16,ACPI_STA_ERROR=32,ACPI_CRS_ERROR=64,ACPI_DEP_ERROR=128,ACPI_SERIAL_CHILD=256,ACPI_ASSOCIATION_ERROR=512,ACPI_PCI_CHILD=1024};
struct acpi_device {
    struct device_acpi_ref reference;uint64_t parent_id,pci_hardware_index,adr;
    uint32_t flags,status,segment,bus,resource_count,cid_count,dependency_count,reserved;
    const char *path,*hid,*uid;const char *const *cids;
    const struct device_resource *resources;const uint64_t *dependencies;
};
struct acpi_table {uint64_t magic;uint16_t major,minor;uint32_t entry_bytes;uint64_t count,generation;const struct acpi_device *entries;};
struct acpi_info {uint64_t devices,errors,associated,unresolved,interpreter_errors;uint32_t ready,runtime_status,stage,sci_enabled;};
/* init(hub,heap,rsdp) loads DSDT/SSDT, initializes AML and publishes one complete
 * boot inventory. Per-device evaluation failures are visible flags, never empty
 * successful resources. Runtime is serialized on CPU0, not IRQ/critical safe.
 * info(out); device(id,generation,out); eval_integer(ref,path,out) and
 * eval_buffer(ref,path,buffer,capacity,out actual_bytes) do not expose AML objects.
 * Buffer failure/insufficient capacity leaves caller contents unchanged.
 * SCI is reserved but kept masked: notification reconciliation is a later step.
 * Tables/resources/strings live until reboot. No interpreter/module unload. */
struct kernel_surface_result acpi_surface_init(MODULE_ARGS);
struct kernel_surface_result acpi_surface_info(MODULE_ARGS);
struct kernel_surface_result acpi_surface_device(MODULE_ARGS);
struct kernel_surface_result acpi_surface_eval_integer(MODULE_ARGS);
struct kernel_surface_result acpi_surface_eval_buffer(MODULE_ARGS);
struct kernel_surface_result acpi_surface_interrupt(MODULE_ARGS);
#endif
