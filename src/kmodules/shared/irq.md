# Device interrupt routing and deferred notifications

irq.kmod provides the device interrupt foundation through versioned hub surfaces.
It is tightly coupled kernel code, not an application-facing resource handler.
Boot allocates 256 slots once, before any source is enabled. Routes use free
vectors 32–223, excluding vectors already bound through inthandle; the upper
range is reserved for kernel timer/IPIs/spurious policy. Exhaustion returns failure.

All foreground route operations and device delivery currently belong to CPU0.
Other CPUs' requests fail. This is explicit ownership of IOAPIC selector/window
access and deferred execution, not a claim of interrupt balancing. The BSP APIC
ID must fit the eight-bit physical destination used by IOAPIC/basic MSI; extended
remapping destinations are not implemented.

irq.route(request,out) validates a versioned deferred callback entry, nonzero
owner and source parameters, claims a vector and returns a fresh monotonically
increasing route ID. A released ID never aliases another route.

- IOAPIC routes take an explicit GSI, level/edge trigger and polarity, locate the
  discovered controller/input, and program a masked physical-destination entry.
  A GSI is exclusively owned; shared INTx, ACPI AML/_PRT and ISA overrides are not
  resolved here. A future bus/firmware routing layer must supply correct parameters.
- MSI routes return a fixed-delivery edge/assert message address/data for the BSP.
  MSI-X uses the same message format. The device driver must program its capability
  or MSI-X table, configure queues and control device-side masking itself. Returning
  a message is not a PCI driver or automatic device enabling.

irq.enable(id,owner,0/1) controls ownership state and IOAPIC masking. For MSI it
controls notification acceptance, not the hardware source. Device owners must
mask/quiesce their device independently before release.

inthandle invokes irq.interrupt(frame,slot,CPU_ENTRY). The ISR uses only
preallocated state and direct validated register mappings. It masks a level
input before sending EOI, increments saturating delivery/pending counters and
returns. Disabled routes acknowledge stale delivery without publishing work.
No allocation, foreground lock, callback invocation or application polling occurs
in this path. APIC setup disables EOI broadcast suppression so IOAPIC remote IRR
can clear. Counter overflow is recorded; pending counts are notifications, not
copies of device packets or completion records.

irq.drain(budget) requires IRQs enabled and no caller-held critical section,
then atomically takes pending counts and invokes callbacks as
callback(notification,context) outside the lock, with IRQs enabled in the normal
event loop. Notifications contain route/owner IDs, vector/GSI/kind and coalesced
count. Their stack storage is borrowed for that call only. Drivers drain their
own device FIFO/queue, clear the cause, and explicitly reenable level routes.
MSI drivers likewise consume device-owned queue state; one IRQ need not equal
one completion. Failed callbacks retain their notification count for retry and
increment failures; drain returns failure, which the current event loop treats
as an essential execution failure. Recursive drain and releasing its active
callback's route are rejected. A rotating cursor prevents a busy low vector
from starving higher vectors across bounded drain calls.

irq.pending() reports routes awaiting foreground work. The event loop wakes its dedicated IRQ worker and checks pending state with IRQs disabled before adjacent
STI/HLT, avoiding a lost device wake. No periodic device polling is added.
irq.info(id,out) reports masking, delivered/pending/drained/failure/overflow
counts and deferred wait in local TSC ticks.

irq.release(id,owner,hardware_quiesced=1) requires a disabled source, no pending
notification or active callback, no local APIC IRR/ISR bit and no IOAPIC remote
IRR. Only then is the vector claim released. MSI hardware quiescence is the
owner's assertion, not something this generic module can verify without its
device driver. Callers must allow in-flight writes/interrupts to settle and retry
when IRR is pending. Context storage must outlive release.

Validation: test-irq models MSI composition, coalescing, callbacks outside locks,
failed-callback retention, ownership/quiescence/IRR checks and stale IDs. Isolated
one/four-CPU TCG boots route a real HPET comparator's level interrupt through an
IOAPIC, check mask/EOI, deferred cause clearing/rearm, repeated delivery and release.
HPET comparator setup is test-only and is restored; production boot enables no
device routes automatically. The read-only NVMe module now uses pci MSI-X
programming and real completion delivery on disposable test media. xHCI, MSI
capability programming, ACPI PCI routing, remapping and affinity remain future work.

References: [Intel HPET specification](https://www.intel.com/content/dam/www/public/us/en/documents/technical-specifications/software-developers-hpet-spec-1-0a.pdf),
[Intel IOAPIC redirection registers](https://edc.intel.com/content/www/us/en/design/publications/core-ultra-processors-for-edge-ps-series-soc-i-o-registers/001/redirection-table-entry-0-rte0-offset-10/).

Normal event dispatch calls drain(1) from a separate preemptible IRQ worker.
A slow scheduler callback cannot monopolize that worker or the event loop;
callbacks within the IRQ worker remain serialized and must be short/bounded.
Move longer work to independent threads. irq.context() identifies the calling
deferred callback context; blocking NVMe operations reject it to prevent waiting
on their own dispatcher. The isolated HPET test verifies completion while a
200 ms scheduler callback is still running.
