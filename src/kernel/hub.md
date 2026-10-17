# Dynamic kmodule hub and provider lifetime

The kernel hub uses ABI **3.0**. A stable, one-page RO/NX header at
`KERNEL_SURFACES_ADDRESS` points to separate method, table-reference and provider
arrays. Each array grows by mapping individual physical pages into its reserved
virtual range. Existing records and mediation callable addresses never move.
There is no 16 KiB registry ceiling or preallocated fixed method-metadata array.

The arrays and stubs each reserve a 16 MiB virtual window. This is address space,
not allocated RAM. Five windows plus their writable aliases reserve 160 MiB in
total, plus the 4 KiB header. Physical pages are committed only on growth; allocation can
fail before a virtual bound is reached. Bounds and 32-bit indexes are checked.
Method records are 56 bytes; table references 72 bytes; provider records 96 bytes;
private invocation metadata 16 bytes; callable stubs 32 bytes. `page_bytes` reports
the header page only. `method_capacity`, `table_capacity` and `owner_capacity`
report fully backed slots, while `mapped_bytes` reports committed physical backing
for the header/arrays/metadata/stubs and `stub_bytes` reports committed stub pages.
Failed construction can retain an unfinished reserved page outside those totals.

Public arrays are RO/NX, stubs RX. Kernel-only writable/NX aliases populate them;
private invocation metadata is also written through a private alias. These are
permissions within trusted kernel space, not isolation against kernel code.
See [startup mappings](../maps/startup.md) for addresses. Inline-array encoding
remains available to ABI3 host fixtures; production uses explicit array pointers.

## Publication and lookup

A nonblocking writer token serializes registration. Concurrent writers receive
FAILED and can retry outside an interrupt context. Page reservation and mapping
use short CPU critical sections; page zeroing, image copying, record construction
and SMP TLB invalidation occur outside an operation-wide CPU lock. Registration
is foreground work, never a hard-ISR operation.

A complete module export batch or table batch is validated before publication.
Duplicate names, including retired method names, cannot be reused. Records are
filled before a release-store advances their published count. Lookup helpers and
iteration counts use acquire loads, so readers see complete immutable records.
A failed allocation/mapping publishes no partial batch; unused capacity may stay
mapped for retry. There is no rollback promise for reserved construction storage.
Module mapping failure retains/quarantines its address range rather than reusing
uncertain mappings. No published entry is relocated or removed.

`kernel_surface_find[_version]` returns methods belonging to LIVE providers.
Already acquired callable stubs remain valid addresses, including after retirement;
a retired call returns FAILED rather than jumping into freed code. Table-reference
pointers remain stable, and callers no longer need to reacquire them after growth.
Provider tables may have mutable internals governed by their own contracts.
Name lookup is still linear. No hash index or performance claim is introduced.

Remote instruction fetch is serialized after stub construction and before module
publication, in addition to the mapping shootdown.

Module image **format3** requires hub ABI3. Format2 images are rejected before
allocation or publication. All bundled modules are rebuilt together. Argument
contracts still live in C headers; C++ layouts are not part of the ABI.

## Ownership

Provider ID zero denotes the kernel. Each successful module load gets a fresh,
never-reused provider ID and a record containing its module name, mapped image
extent, policy flags, method/table counts, active calls, leases and lifecycle.
Method ownership comes from the loader. A table publisher explicitly sets
`kernel_hub_table.owner`, normally from `kernel_surface_owner(hub,"module.init")`.
Unknown and non-LIVE table owners are rejected. Ownership is inspectable metadata
and a lifetime contract, not caller authentication in shared kernel space.

The hub owns copied descriptors, invocation metadata, callable stubs and provider
records until reboot. The provider owns the table's pointed-to backing and must
keep it valid until reboot. Publishing a table permanently pins that provider's
lifetime: draining/retirement is refused while it has published tables. There is
no table-withdrawal API because current readers borrow pointers without release
tracking. Heap allocation/free cannot enforce that trusted-provider obligation.

All current Limine-loaded modules use `HUB_OWNER_BOOT_PINNED`, including hard-ISR
providers and foundational dependencies. They cannot drain or retire. Their gate
path omits active-call atomics because their lifetime cannot end. Kernel tables
likewise remain pinned. `kernel_module_load_owned(...,flags=0)` permits cooperative
retirement for selected future modules; it is a kernel API, not a disk loader.
Current modules stay in the boot partition; only selected future modules will
be loaded from ext4.

## Draining callers

The exposed surfaces are:

- `hub.owner_info(id,out kernel_hub_owner)` snapshots state, active calls, leases,
  publications, image extent and policy. Mutable counters are individually atomic;
  the entire diagnostic snapshot is not one indivisible instant.
- `hub.owner_pin(id,1)` acquires a lease only while LIVE; `(...,0)` releases one
  while LIVE or DRAINING. Underflow/overflow fail. Pins for boot-pinned providers
  are no-ops, since those providers can never retire.
- `hub.owner_control(id,HUB_OWNER_DRAIN)` transitions LIVE to DRAINING. Repeated
  drain requests succeed. New name lookup and new leases stop immediately.
- `HUB_OWNER_RESUME` restores a DRAINING provider to LIVE.
- `HUB_OWNER_RETIRE` succeeds only for a DRAINING provider with zero active calls,
  zero leases and no published tables. Retirement is one-way and never reuses names.

Drain and retire are nonblocking. The caller observes progress through owner_info,
quiesces producers, releases dependencies and waits through its own event/deadline
policy. Blocked callers are not killed or forcibly unwound.

For a drainable provider, the mediation gate atomically accounts for calls across
preemption/blocking and decrements the count only after the implementation returns.
Existing leases and active call chains may continue using previously acquired
stubs while draining, allowing nested work to finish. When both counts reach zero,
the gate refuses entry. A retained raw pointer does not itself count as a lease:
a caller repeatedly invoking an old stub while other calls/leases remain can
extend draining. Quiescing those producers is part of the retirement protocol;
there is no timeout that makes unsafe reclamation safe.

Scheduler METHOD records acquire a provider lease at submission and keep it until
the record is reclaimed, including completed/cancelled retained results. Thread
records keep a lease through exit until successful reap. Deferred IRQ routes keep
a callback lease until masked, pending-free, non-running and hardware-quiesced
release. Failed construction releases its acquired lease. New objects targeting a
draining provider fail. Long-lived cached dependencies outside these facilities
must acquire/release leases explicitly. A kernel provider's process/resource owner
numbers are separate namespaces from these module-provider IDs.

Hard interrupt bindings accept only boot-pinned providers. They cannot be made
retirable with a foreground call counter alone. Existing device drivers use a
boot-pinned IRQ front end and foreground deferred callbacks.

## What retirement does not do

Retirement stops future mediated execution; it does **not** unmap images, reclaim
stubs/records, withdraw tables, change hardware ownership or perform hot replacement.
Keeping executable mappings is necessary until dependency callbacks, raw/native
references, private worker teardown and table-reader leases have comprehensive
reclamation rules. Hardware owners must still stop/reset devices and drain DMA.
Provider failure or a never-returning caller may prevent retirement indefinitely.

Validation: `test-hub` uses real generated RX stubs, concurrent host readers and a
blocked caller, injects allocation and writable-alias mapping failures, grows past
600 methods/200 tables, and checks transactional publication/stable pointers,
lease/active draining, resume, retirement and stale rejection. `test-hub-boot`
validates late real module loading, old-format rejection, RO/RX guest page flags,
600 methods/200 owned tables, execution of late stubs on a second CPU, blocked threads, scheduler/IRQ leases and retirement
on isolated four-CPU QEMU. No candidate VM or persistent developer disk is used.
