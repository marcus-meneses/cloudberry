# Private address spaces

`mem.space_*` provides trusted kernel address-space facilities, backed by the
kernel's page-table/CPU mechanisms. These surfaces are not application handles,
syscalls, a process manager or a userland ABI. Handler/handle architecture remains
pending discussion. There is no ring-3 execution, executable loader, user-copy
boundary, demand paging, swap, COW, PCID or migration in this milestone.

## Layout and ownership

An address space has a never-reused ID, nonzero process owner, private PML4 and
private lower-half tables. User mappings span page-aligned addresses from 4096
through the page below `0x0000800000000000`. Page zero and the noncanonical/upper
half are rejected. Each user leaf and intermediate entry has U/S set. Read is
implicit; flags select write or execute, with simultaneous W+X rejected per leaf.
This is page permission enforcement, not a ban on trusted-kernel HHDM aliases.

All upper-half slots reference shared kernel PDPTs with supervisor-only PML4
entries. Kernel/module/hub/MMIO/heap/stack mappings therefore remain accessible
to the kernel under every root and inaccessible to ring 3. Before AP startup,
bootstrap fills absent upper slots with empty shared PDPTs (at most 1 MiB once,
not per process). Later kernel mappings work even in formerly empty slots.
Kernel mappers always edit the captured master root rather than the current CR3.

An empty space commits one 4 KiB root and a 64-byte registry record, plus allocator
metadata. A first user page normally needs three further 4 KiB table pages, plus
its separately allocated data page. More tables grow on demand; no fixed process
or mapping count is imposed. Existing shared kernel tables remain boot-owned.
Four-level paging and NX are required; LA57 and PCID are explicitly rejected.
CR0.WP is enforced on BSP and APs. Root switches discard inherited global as well
as nonglobal translations; process leaves are never global.

## Surfaces

Allocating/mutating operations are foreground work, never ISR callbacks; switch
is the scheduler exception. All versions are 1.0; declarations and descriptors live in `mem-spaces.h`.
The mem frontend binds `memory.space_*` mechanisms through the hub.

- `create(owner,out info)` creates an empty READY space.
- `map(id,owner,va,physical,flags)` maps one existing owned PAGES frame.
- `unmap(id,owner,va)` removes one user mapping.
- `protect(id,owner,va,flags)` changes permissions on an existing user mapping.
- `query(id,owner,va,out mapping)` copies its physical frame and permissions.
- `info(id,owner,out info)` copies ID, owner, root, active CPUs, leases and state.
- `pin(id,owner,acquire)` retains/releases a scheduler lifetime lease.
- `switch(id,owner)` is a scheduler-only operation with IRQs disabled and a lease;
  `(0,0)` restores the kernel root. Callers must use the thread machinery rather
  than changing their current thread's root behind its saved context.
- `destroy(id,owner)` requires IRQs enabled and no outer critical section. It
  rejects active/leased spaces, marks DESTROYING and reclaims
  private tables using bounded disposal steps. New mappings, switches and leases
  reject that state. Unexpected disposal failure retains the record for retry.

Each user mapping increments the physical reservation's mapping count. Wrong
owners, non-PAGES storage and duplicate mappings fail. Unmap/protect waits for
TLB acknowledgements from CPUs currently using that space while retaining the
ledger lock, so data cannot be released/reused before stale translations vanish.
Shared kernel changes invalidate every online CPU. A root switch and its CPU
identity update serialize against these changes. PCID is disabled, so inactive
roots retain no usable private translations. Mailbox waits service invalidations
while contending for the CPU lock; watchdog failure remains fatal.

Destroy drops mapping references and frees private page tables; it does not free
user data allocations. Their owner must release them separately after references
have drained. Empty intermediate tables, including partial construction after
OOM, remain reachable until destroy. Disposal releases the lock between individual
leaf/table removals. Kernel metadata heap backing and shared tables remain owned
until reboot, as with the existing allocator.

## Thread integration

`thread_request` ABI1 preserves its existing prefix and uses the kernel root.
ABI2 appends `address_space`; zero still selects the kernel root. A nonzero ID
must belong to `process_id`. Creation leases the space, every context switch
selects the saved root with IRQs disabled, and dispatchers return to the kernel
root. Timer preemption, voluntary yield/block and exit all use this path. The
lease survives EXITED until REAP, preventing teardown while saved contexts exist.
Threads remain kernel-mode and pinned to their selected CPU.

## Validation

`make -C src test-spaces` covers private roots, shared future kernel mappings,
ownership, canonical bounds, W^X, aliases/references, permission changes, teardown
and partial table-construction OOM using the production page-table code.
`test-spaces-boot QEMU_CPUS=4 QEMU_IOMMU=amd|intel` uses isolated media and
kernel-only probes: same user VA with distinct backing, real CR3 switches,
timer preemption, yield/block/resume, remote active-root permission invalidation,
lease-gated destruction, reaping and stale IDs. Eight full cycles also attempt
real read-only writes, NX execution and reads after unmapping; exact #PF error,
CR2, CR3 and instruction/fixup addresses must match. The test-only CPU-local
page-fault gate is restored after each guarded operation. CPU1 keeps IRQ/IPI
handling enabled but defers thread switching during revocation, preventing a
context-switch flush from hiding stale translations. Each cycle returns owned
page/table/stack reservations to its baseline. No probe enters candidate images,
and production exception dispatch is unchanged.
The same target measures paired root-selection controls, remote/inactive
protection, yield elapsed time and dense/sparse teardown with raw host reports;
see [profiling methodology and observations](../../tests/address-space-profiling.md).
No production scheduling/TLB optimization is inferred from TCG-only timings.
Ring-3 fault containment and the eventual handler invocation boundary remain
separate acceptance milestones.
