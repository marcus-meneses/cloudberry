# Extended thread context

`xstate.kmod` implements eager x87/SSE and optional AVX context preservation for
both Intel and AMD. BSP calls `xstate.cpu_init()` before thread initialization;
each AP initializes its local CR0/CR4/XCR0 and must match the BSP layout before
becoming online. Different enabled feature/layout geometry fails AP startup.
`cpu_init` is a bootstrap operation, not a runtime thread API.

The module enables native floating-point exceptions, clears EM/TS, enables SSE
context support, and enables OSXSAVE when supported. XCR0 includes x87 and SSE,
plus AVX only when both CPU and XSAVE component support are present. The selected
standard-format buffer size/AVX offset come from CPUID, rounded to 64-byte
alignment. Without XSAVE, 512-byte FXSAVE64/FXRSTOR64 preserves x87/MMX/SSE.
AVX-512, AMX and other extended components remain disabled deliberately.

`info(out)` reports mask, buffer size, alignment, format and AVX geometry.
`init(buffer,bytes)` creates clean architectural state with default x87 control
word and MXCSR; new contexts do not inherit the creator's registers.
`save(buffer,bytes)` and `restore(buffer,bytes)` require trusted, aligned buffers
of at least the reported size. They allocate nothing and take no locks. Malformed
save images are kernel programming errors, not accepted user input.

Threads reserve one buffer per thread and dispatcher CPU. With interrupts disabled,
each switch saves the outgoing extended state and restores incoming state before
switching stacks. This also covers a thread suspended from a timer interrupt.
Reaping frees the thread's embedded state with its record. CPU pinning remains.

Kernel dispatch, ISR code and ordinary kmodules still build with
`-mgeneral-regs-only`: interrupt handlers must not clobber the interrupted thread's
vector registers before it can be saved. Explicit workloads can now use supported
floating-point/vector instructions; changing the global compiler flags is not a
substitute for interrupt-state ownership. User processes, lazy FPU ownership,
thread migration and SIMD in arbitrary interrupt callbacks are not implemented.

Isolated probes hold different x87 values, XMM0/XMM15 or YMM0/YMM15, MXCSR and
control words across voluntary yields and repeated timer preemption. They also
check fresh state and run on every CPU. Separate XSAVE/AVX, XSAVE/SSE-only and
FXSAVE boots exercise the capability choices. Tests never enter candidate images.

In the current QEMU 6.2 environment, guest XSAVE/SSE and FXSAVE paths pass;
AVX is unavailable in TCG. `test-xstate-host` exercises the actual production
XSAVE/XRSTOR routines on the native host with x87/SSE/AVX and control-state
round trips. An earlier user-host KVM report on Ryzen 5 5500U records guest AVX
preemption on four CPUs: XSAVE mask 7, 832-byte state, eight probe threads,
zero preservation errors and 10–12 dispatches per thread.

Latest-run status (2026-10-14): both corrected four-CPU KVM runs pass on Ryzen
5 5500U, with matching report/result run IDs, error-free x87/SSE/AVX preemption,
NVMe and emulated Intel VT-d/AMD-Vi DMA revocation. Current APIC mode is 1 (xAPIC);
the earlier x2APIC result remains archived historical evidence. TSC is UNAVAILABLE
and shared timestamps remain HPET. Physical Intel/AMD devices remain unqualified.
