# apic

`apic.kmod` owns BSP interrupt-controller bring-up. Boot calls apic.init after
MMIO and clock initialization, before timer. It uses the firmware's existing
xAPIC or x2APIC mode; AP startup belongs to smp; no mode transition is performed.

- apic.init(hub) claims spurious vector 255 through inthandle, maps the local
  controller when using xAPIC, masks discovered IOAPIC inputs and legacy PIC
  inputs, configures common local LVT masks/TPR, preserves a LINT1 NMI route,
  and enables the local controller with its owned spurious vector.
- apic.info(out apic_info) returns the stable register-access descriptor, mode,
  physical address, spurious vector and cumulative spurious count. Cooperating
  kernel clients may access their delegated registers using this descriptor.
- apic.spurious is a claimed CPU-entry callback; it records delivery without EOI.

Controller-wide configuration belongs here. Timer receives the descriptor and
programs only its timer registers and acknowledges its timer IRQ; it does not
mask PIC/IOAPIC inputs or configure unrelated local sources. Descriptor sharing
is kernel cooperation, not a protection boundary. apic.cpu_init() validates the established mode/address and configures this CPU's
local LVT masks/TPR/SVR without repeating global PIC/IOAPIC work. SMP invokes it
on APs; spurious counts are atomic across CPUs.
Failure before controller activation releases this attempt's claim; already
installed mappings/input masks remain. No external device routing is provided.

Host checks cover xAPIC/x2APIC descriptor selection with fake hardware, claim
rollback, masking, NMI-route preservation and spurious delivery without EOI.
The isolated modern-bus TCG boot validates actual xAPIC controller/timer operation
and both owners' vector protection. User-host four-CPU AMD KVM acceptance
recorded x2APIC (reported mode 2), wake IPIs and TLB reuse.
Physical motherboard interrupt routing remains unvalidated.

```sh
make -C src test-apic
make -C src test-events-boot
```


Local setup clears EOI broadcast suppression so level IOAPIC routes can clear
remote IRR. irq owns subsequent explicit device redirection entries and source
masking; apic's initial global masking still runs only at boot.

Source layering: apic.c owns shared frontend policy/exports; apic-x86.c supplies
the xAPIC MMIO/x2APIC MSR adapter used by both Intel and AMD. They link into one
apic.kmod. CPU-specific ID/timestamp/fence operations live in the selected
cpu-intel or cpu-amd kmodule.

Latest-run status (2026-10-14): both corrected four-CPU KVM runs pass on Ryzen
5 5500U, with matching report/result run IDs, error-free x87/SSE/AVX preemption,
NVMe and emulated Intel VT-d/AMD-Vi DMA revocation. Current APIC mode is 1 (xAPIC);
the earlier x2APIC result remains archived historical evidence. TSC is UNAVAILABLE
and shared timestamps remain HPET. Physical Intel/AMD devices remain unqualified.
