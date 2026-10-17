# inthandle

`inthandle` is the interrupt/exception kmodule. Limine loads it first, and the
kernel registers its named exports through the same generic loader used for
other kmodules. The kernel calls `inthandle.init` before loading remaining
modules or drawing the framebuffer. The kernel contains no separate IDT or
exception-dispatch implementation.

The module owns descriptor-table initialization and all 256 IDT vector stubs.
Its early BSP tables are installed before mem is available. Normal entry uses the
interrupted stack; NMI, double fault and machine check use three dedicated 8 KiB
IST stacks. Register-saving assembly normalizes vector/error-code frames, aligns
the stack for C, preserves general registers and returns through iretq. The
module's writable state and stacks are reserved module pages mapped RW/NX;
code/exports are RX. These are CPU interrupt entries, separate from normal
synchronous kmodule calls.

Hub methods (uniform six-word call ABI; headers define payloads):

- `inthandle.init()` installs the BSP tables, idempotently.
- `inthandle.bind(vector, surface_name, context)` binds vectors 32..255 to an
  existing major-version-1 hub method. A NULL name removes the binding. Exceptions are currently
  fatal and cannot be rebound.
- `inthandle.check(vector)` returns its delivery count.
- `inthandle.is_bound(vector)` reports whether a callback is bound.
- `inthandle.dispatch(frame)` dispatches an explicit valid frame. Real CPU entries
  use the CPU-entry path after saving registers. Manual dispatch rejects
  exceptions/unbound vectors and returns failure on callback failure.

The bound method is invoked through its mediated hub entry with
`(frame, context, delivery, 0, 0, 0)`, where delivery is INTHANDLE_CPU_ENTRY
for a real CPU entry and INTHANDLE_DEFERRED for manual/scheduled delivery.
Bind/dispatch minor version1 defines this tag. It can edit saved registers and must return success.
On a real CPU entry, an unbound vector, failed callback or exception prints vector/error, RIP/RSP,
flags, CR2/CR3, segment selectors and all general registers, then halts. The fault
path does not allocate or depend on another module. This is a register/context
dump, not a symbolic backtrace.

Timer/event now enable the owned BSP local APIC timer IRQ. Timer acknowledges
its own hardware delivery; external device inputs remain masked until an owned irq route is enabled. Future device
support must establish ownership, routing and acknowledgement before unmasking.
inthandle.cpu_init(storage,IST_tops) installs CPU-private GDT/TSS/IDT, using
three caller-supplied guarded stacks; a zero first argument queries storage size.
SMP replaces the early BSP tables/stacks and installs these on every AP.
Interrupt binding writers are serialized by kernel mediation. Hardware/deferred
readers snapshot callback/context/owner through atomic sequence counters; delivery
counts are atomic. Owners must quiesce all CPUs' in-flight callbacks before
reclaiming context memory after replacement/release.
The few instructions needed to load this first module still precede owned fault
handling; a corrupt loader cannot be diagnosed by a module it has not loaded.

Validation: the normal boot test binds a test-only module method to vector128,
executes int80, checks context and return-register preservation, checks delivery
count and unbinds. `test-inthandle-faults` uses separate kernels to raise #UD,
#PF (checking CR2/error code), and #DF with an unusable original stack. Each
must print the expected context and halt. No injection is in the production ISO.

Reference: [AMD64 system programming manual, exceptions and interrupts](https://docs.amd.com/v/u/en-US/24593_3.44_APM_Vol2).

The scheduler uses is_bound and dispatch for deferred copied frames. Delivery
counts include these valid manual deliveries, but no CPU IRQ is generated.

`claim(vector,name,context,owner)` exclusively binds a free vector; owner must be
nonzero. `release(vector,owner)` requires the same owner, after its hardware is
quiesced. Ordinary bind/unbind cannot replace a claim. Manual dispatch rejects
claimed vectors, preventing deferred delivery from issuing a hardware EOI.
Binding mutations save/disable/restore IF. Owner tokens identify cooperating
kernel modules; this is not a security boundary against other kernel code.

Inthandle also owns the CMOS selector/NMI-mask shadow. Init explicitly masks NMI
before activating its tables, then enables it after initialization. `cmos_read`
preserves selector, mask and IF, and rejects acknowledgement register C.
`nmi_mask(0/1)` sets policy and returns its previous mask; argument 2 only queries.
No code guesses the old mask by reading the write-only selector port. Clock
uses this surface for RTC reads. A boot check deliberately masks NMI, reads the
RTC and verifies the mask remains set before restoring the previous policy.

Hard-ISR bindings require boot-pinned providers. Retirable modules use foreground
deferred IRQ callbacks, whose routes lease provider lifetime. See [hub lifetime](../../kernel/hub.md).
