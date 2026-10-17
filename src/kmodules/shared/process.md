# Protected processes

`process` is a boot-pinned kernel mechanism. It loads static ELF64 ET_EXEC images
from an already selected ext4 filesystem into owned lower-half pages. Code is
RX, data/stack RW/NX; writable executable pages, overlapping LOAD pages,
interpreter/dynamic/TLS segments, malformed ranges and noncanonical addresses
are refused. Empty segments are ignored only when their file size is also zero.
Each process has one CPU-pinned preemptible thread, a guarded kernel execution
stack and a separate guarded kernel-entry stack. User stack size is supplied by
trusted bootstrap policy (64 KiB default), committed eagerly; demand growth,
TLS, dynamic linking, arguments/environment and multi-threaded processes are
not part of this initial milestone. IDs are never reused in one boot.

The GDT includes user selectors 0x2b/0x33. Per-CPU TSS RSP0 follows the saved
thread's kernel-entry stack before switching. User interrupt entry/return owns
SWAPGS; FSGSBASE stays disabled and user RFLAGS are sanitized. IRQ/timer paths
remain preemptible outside existing critical sections. CPL3 synchronous faults
terminate only the current process and record vector/CR2/RIP. Kernel faults,
NMI, double fault and machine check retain their fatal handling.

The DPL3 vector 128 gate currently accepts lifecycle exit (RAX=0, RDI=status).
This is the bootstrap for the copied handle gate, not a resource syscall family.
A fault/exit returns through the normal interrupt unwind into the process's
retained kernel continuation. The enclosing thread must actually reach EXITED
before reap can free either kernel stack or the private address space.

Trusted hub surfaces: `init(hub,heap)`, `load(process_request)` returns process ID,
`start(id)` returns thread ID, `info(id,out)`, `reap(id)` and `current()` (zero
outside a process's running thread). `copy(id,user,kernel,bytes,write)` copies at
most 4096 bytes after validating the complete range against owned mappings;
write requires a writable user leaf. Physical aliases avoid user-copy faults.
The caller supplies authenticated process identity; these surfaces are never
mapped or callable from ring 3. No user pointer survives a copied request.

The initial process loader is not an installer or a signature verifier. Only
trusted bootstrap/kernel policy may admit an executable; arbitrary apps cannot
load or assign permissions to another process. Resource manifests/handle grants
are the next milestone. Process exit can retain its result and memory until its
kernel owner explicitly reaps it. If a failed load cannot finish rollback, it
returns failure with a cleanup-only process ID for retry through reap; that
record cannot be started. Normal load failures return failure with zero.

`test-process-elf` checks malformed ELF bounds/permissions; `test-process-boot`
uses disposable ext4 media and test-only executables. It validates same-VA private
backing, preemption, contained kernel/RO/NX/unmapped accesses and CLI, user GS
selector changes, stale process IDs and allocation/stack baseline restoration.
Fixtures never enter candidate media.

Local acceptance (TCG): one-CPU/no-IOMMU run `c4c028b695ed409a848b6700b11e4037` and
four-CPU/VT-d run `1d3609b7d8d24e84ac89c2c8f9ece5f9` pass. The final four-CPU
probe also verifies NT-flag sanitization across the user gate. Pure ELF decoder
checks pass under UBSan. Native KVM/physical user-entry acceptance remains open.
