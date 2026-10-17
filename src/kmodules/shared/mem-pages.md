# Managed pages and guarded stacks

The mem kmodule now owns releasable page/stack allocation policy in mem-pages.c.
It reuses the kernel's live physical reservation ledger rather than creating a
second free-memory authority. The boot allocator still establishes the ledger,
module/hub mappings and the initial heap before mem can operate.

Public structures and surfaces are in [mem-pages.h](mem-pages.h):

- mem.pages_init(hub,heap) binds the ledger/mapping services and metadata heap.
- mem.pages_alloc(count,owner,out) allocates zeroed contiguous physical pages and
  returns a descriptor containing a non-repeating ID, physical base, HHDM address,
  count and owner. Owner zero identifies kernel-owned memory.
- mem.pages_free(id,owner) releases only the exact live allocation, with matching
  owner and no outstanding managed mappings. Stale IDs and wrong owners fail.
- mem.stack_alloc(bytes,owner,out) allocates independent physical frames, maps a
  page-rounded RW/NX body between two unmapped guard pages, and returns ID,
  bottom/top and actual body size. Physical contiguity is unnecessary.
- mem.stack_free(id,owner) rejects executing stacks and registered CPU dispatcher/emergency stacks, unmaps its body
  and releases frames. Virtual gaps can then be reused with a fresh ID.
- mem.pages_info(out) reports live allocations, physical pages, stacks and stack
  pages, independently of permanent heap/page-table backing.

Stacks use a dedicated virtual window [0xffffa00000000000,0xffffb00000000000).
First-fit gap search includes both guards, preventing managed stack collisions.
Backing is committed immediately for the requested body; guards consume virtual
addresses only. No fixed 16 KiB stack size or stack-count ceiling is imposed.
Representable sizes, window capacity, metadata/backing memory and page-table
allocation can still limit a request. Stacks are allocations, not running threads.

Kernel services enforce physical ownership and managed mapping references:
memory.reserve 1.1 optionally returns physical base in its fourth argument;
memory.release(physical,count,kind,owner) frees only PAGES/STACK kinds;
memory.map_owned(virtual,physical,owner) installs an exclusive RW/NX leaf and
increments its allocation's reference count; memory.unmap_owned removes only
that exact leaf and invalidates its local TLB entry and waits for remote acknowledgements before
physical reuse.
A referenced allocation cannot be released. Permanent module/hub/heap/context/
ledger/page-table allocations are protected from this release API.

The reservation table is version 3: its mappings field counts managed CPU mappings plus DMA pins. Release sanitizes pages before removing their reservation;
subsequent allocation also starts zeroed. Removal compacts records within their
chunk, so consumers must not retain pointers to individual ledger entries.
The original Limine map remains unchanged. Reusable gaps become available to
both boot and managed allocations.

Allocation rollback needs no new allocation and releases every acquired frame
and mapping. Failed requests do not modify output descriptors. If an unexpected
backend cleanup failure occurs, the failed result carries a retained cleanup ID
for retry through stack_free; ownership metadata is not silently discarded.
A failed free may have unmapped/released a prefix; retained metadata permits
cleanup retry, but the allocation must no longer be used.

Foreground mutations use the recursive CPU lock. Stack operations acquire it
internally around short transitions and authority calls, rather than retaining
it over the whole operation. Callers must quiesce every user
before freeing. HHDM and firmware-read aliases are trusted kernel facilities,
not managed mapping references or protection against malicious kernel code.
Private process-root facilities are described in [mem-spaces.md](mem-spaces.md);
user execution and demand paging remain absent. Shared-kernel and active private
root TLB shootdowns use CPU mailboxes/IPIs.
Empty page-table/ledger chunks and metadata heap backing remain reserved; ordinary
malloc/free still reuse heap blocks without returning their backing pages.

```sh
make -C src test-reservations
make -C src test-pages
make -C src test-events-boot
```

Host checks cover exact ownership, sanitization/reuse, stale IDs, mapped-release
rejection, fragmented stack backing and rollback after partial failures. The
isolated TCG boot checks real leaf permissions, both absent guard pages, mapping
references, unmapping and reuse. A test-only assembly helper calls stack_free
while executing on an allocated stack, verifies rejection and restores the
original stack. It is excluded from the candidate image and adds no thread API.

Stack construction publishes an in-progress gap reservation, then reserves/maps
frames outside a single allocation-wide lock. Construction/free ownership blocks
concurrent disposal while retaining the virtual gap. Mapping and unmapping use
batches of at most 16 pages, with one acknowledged SMP shootdown per batch.
Unmapping validates the complete batch before mutation; partial map failure
reports its mapped prefix so rollback accounts for every mapping. Frames are
released only after invalidation acknowledgements. Stack free and thread reaping
no longer retain the thread-list/global lock over the whole teardown. Individual
physical reservation/release and page-table operations remain serialized; large
contiguous page allocation/free can still hold the allocator lock during zeroing.

Reservation construction/release now uses a SCRUB state: physical ranges remain
reserved and cannot be mapped/released while bulk zeroing occurs outside the
ledger lock. Final publication/reuse occurs under that lock after sanitization.
DMA pinning blocks physical release until acknowledged IOMMU invalidation. Heap
wrappers likewise keep large zero/copy/growth work outside heap metadata locks;
raw allocator primitives retain caller-synchronization requirements.
