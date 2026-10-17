# ACPI and platform device inventory

`acpi.kmod` owns a pinned uACPI 6.1.1 runtime, AML objects and the boot device
namespace. `hwenum` remains the immutable raw firmware/PCI inventory; it does not
interpret AML. `pci` remains the owner of PCI configuration and driver claims.
These are trusted, coupled kernel modules, not application handles.

## Contracts and identities

[device.h](device.h) defines boot-stable device IDs, generation-qualified ACPI
references, parent identities, owner fields and resource descriptions. A PCI
record carries an optional `{id,generation}` ACPI companion. The module resolves
that reference to its private namespace node; interpreter pointers never appear
in a public device record. PCI and ACPI descriptions of the same hardware do not
create two independently claimable devices. IDs are not persistent across boots.

Three owned tables supplement `hwenum.hardware`:

| Table | Backing and lifetime |
| --- | --- |
| `pci.devices` | Static header, exact-size heap vector for all enumerated PCI functions (at least one record allocated); owner/companion fields update through PCI surfaces |
| `acpi.devices` | Static header, heap vector initially 16 records, doubling on demand; private node vector grows alongside it; paths, IDs, resources and dependencies are separately owned allocations |
| `platform.devices` | Static header, exact-size heap vector of eligible firmware devices (at least one record allocated); resources/strings reference the ACPI module's stable backing |

Each hub entry references one table header; use its `entries` pointer and `count`,
not the hub reference's count, to walk devices. ACPI and platform snapshots remain
stable after initialization and live until reboot. Ownership is not granted by
resource enumeration. No image unload or dynamic namespace reconciliation exists.

## Boot initialization

`acpi.init(hub,heap,rsdp_physical)` runs on BSP after clock, threads, IRQ and PCI
initialization. It validates/loads DSDT and SSDTs, initializes the namespace and
executes firmware `_STA`/`_INI`/operation-region initialization through uACPI. A
failed AML-table load fails startup rather than silently advertising completeness.
Boot configuration passes the RSDP; interpretation stays inside this module.

Each Device record includes its path, `_HID`, `_CID`, `_UID`, `_STA`, optional
`_ADR`, `_SEG`, `_BBN`, decoded `_CRS` and `_DEP` device references. Missing optional
objects are normal; evaluation/type errors set explicit flags. `_STA` failure is
not reported as a healthy device. Unresolved dependency slots are zero with an
error flag. `acpi.info(out)` reports readiness, initialization stage/status, device
counts, companion counts, device errors and interpreter error-log count.

PCI root identification uses PNP0A03/PNP0A08 HID/CIDs. Matching uses segment, root
bus (`_BBN`, otherwise the `_CRS` bus minimum, otherwise zero), `_ADR` slot/function
and the associated PCI bridge's secondary bus. Segment defaults to zero. Matching
never uses vendor/product IDs. Duplicate roots/BDFs and ambiguous bridges remain
unbound; their descendants cannot inherit an ambiguous association. PCI accepts
companions only before driver ownership and rejects replacement by a different
reference.

`platform.init(hub,heap)` publishes HID/CID-bearing non-PCI devices. It excludes
PCI roots, PCI companions/unresolved PCI-addressed children and serial-bus children.
An `_ADR` on a genuine non-PCI device alone does not exclude it. Parent identities
refer to the nearest included platform device or PCI companion. This table is
an inventory, not a general platform driver manager.

## Resources and evaluation

Resource descriptions cover memory, I/O ports, IRQs, DMA channels and bus ranges,
including producer/shared/trigger/polarity/read-only flags. Address translations
are recorded, not applied as claims. Small Memory24 units are normalized to bytes.
Serial/GPIO/unsupported descriptors remain explicitly unresolved; a future bus
owner must interpret/claim them. Dependent alternatives are not flattened into
unconditional claims. `firmware_type` and original descriptor index identify the
source; callers can retrieve the raw `_CRS` using buffer evaluation. This is not
complete GPIO/I2C/SPI/AML routing or driver support.

`acpi.device(id,generation,out)` returns a stable record snapshot.
`acpi.eval_integer(ref,path,out)` and
`acpi.eval_buffer(ref,path,buffer,capacity,out_actual)` evaluate no-argument
objects/methods. Errors preserve the integer/buffer output; an undersized buffer
reports the needed size without copying any bytes. These are trusted kernel
surfaces, not an authorization boundary. Firmware methods can have side effects.

## Host adapters and boundaries

The interpreter uses hub memory allocation, clock timestamps, thread sleep/yield,
PCI firmware access, firmware mappings, port I/O and debug output. Public execution
is serialized on CPU0 and refuses an existing CPU critical section. Never call
AML evaluation from a hardware ISR. Spinlocks/work queues rely on this CPU0-only
execution model; no AP or ISR executes AML. Sleep uses timed thread blocking when
there is a running kernel thread, otherwise the bootstrap clock wait.

Firmware pages are explicitly mapped through `hardware.firmware_map` using the
existing checked UC physical-read window. It permits reserved/gap and ACPI
reclaimable/NVS pages, rejects usable RAM, loader/kernel storage and framebuffer
pages, and verifies the UC PAT slot. It does not assume Limine mapped ACPI into
HHDM. Mappings/page tables remain until reboot. Allocating an operation-region
mapping does not transfer allocator ownership of those pages.

`hardware.port_read/write` perform exact-width accesses and deny kernel-reserved
PIC/PIT, PS/2, CMOS/NMI, debug and PCI configuration ports. PCI firmware reads use
the enumerated ECAM records. Writes refuse claimed endpoints, headers/BARs,
extended configuration and the standard capability area. This conservative
policy may reject firmware that needs broader access; it must not bypass an
existing controller owner. CMOS/NMI ownership remains with `inthandle`.

SCI installation reserves a masked IOAPIC route, applying matching MADT interrupt
source overrides. `sci_enabled` remains zero. ACPI mode transition, enabling SCI,
GPE/Notify reconciliation, embedded-controller operation regions, power-management
policy, PCI `_PRT`/link-device routing and platform drivers remain future work.
Pending uACPI work uses preallocated work items drained in BSP foreground; it is
not routed to allocating scheduler calls from ISRs. Enumeration alone does not
establish physical firmware compatibility.

## Image and validation

Only this module uses a private PIE link to accommodate upstream static pointer
tables. Private fixup handling stays inside ACPI; the current format3 loader requires hub ABI3. `acpi.init` validates
and applies local `R_X86_64_RELATIVE` fixups to writable module state once; export
records stay relative and read-only. The build checks relocation types, bounds,
unique targets and linker marker extents. GOT dependencies are rejected.
Vendored sources and MIT license are preserved; see [provenance](acpi/vendor/README.md).

`make -C src test-acpi` validates segmented/bridged matching, ambiguity refusal,
platform filtering/parentage, generation checks and publication rollback under
UBSan. `make -C src test-acpi-boot` uses a disposable VM and test-only SSDT with
known resources, dependency, method/buffer values and malformed `_CRS`. The fixture
never enters candidate firmware or media. Isolated one-CPU and four-CPU Intel/VT-d
and AMD/AMD-Vi TCG acceptance pass. Native KVM and physical ACPI acceptance of this
new subsystem remain pending; earlier native clock/SMP results do not cover it.

Resource encodings follow the [ACPI device-configuration specification](https://uefi.org/htmlspecs/ACPI_Spec_6_4_html/06_Device_Configuration/Device_Configuration.html).
Memory24 addresses/lengths use 256-byte units, while its alignment is already in
bytes (zero means 64 KiB); the injected SSDT tests both alignment cases separately.
