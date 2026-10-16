#ifndef CLOUDBERRY_KMODULE_INTHANDLE_H
#define CLOUDBERRY_KMODULE_INTHANDLE_H
#include "module.h"
struct inthandle_frame {
    uint64_t r15,r14,r13,r12,r11,r10,r9,r8,rbp,rdi,rsi,rdx,rcx,rbx,rax;
    uint64_t vector,error,rip,cs,rflags,rsp,ss;
};
enum inthandle_delivery { INTHANDLE_CPU_ENTRY=0, INTHANDLE_DEFERRED=1 };
/* A bound hub method receives (frame, context, delivery, 0, 0, 0), returns status,
 * and may edit saved registers before iret. It runs with maskable IRQs disabled. */
/* init establishes early BSP tables; cpu_init(storage,three_IST_tops) installs
 * CPU-local GDT/TSS/IDT (a=0 queries storage size). SMP supplies guarded stacks.
 * Timer/event enable owned local APIC IRQ delivery. init owns dedicated
 * NMI/double-fault/machine-check stacks. Exceptions <32 dump and halt.
 * bind(vector,surface_name,context): vectors32..255 only; NULL name unbinds.
 * The method is resolved from the hub. Callback must perform
 * its own controller acknowledgement; timer owns local APIC timer EOI.
 * check(vector) returns delivery count; is_bound(vector) reports callback presence. dispatch(frame) is also exported; manual dispatch rejects exceptions/unbound
 * vectors without halting. Real unhandled CPU entries still dump and halt. */
/* claim(vector,name,context,owner): exclusive binding; owner must be nonzero.
 * release(vector,owner): matching owner only; hardware and in-flight callbacks
 * on every CPU must first be quiesced before releasing their context storage.
 * bind cannot replace/unbind a claim; manual dispatch rejects claimed vectors.
 * cmos_read(index): preserves selector/NMI mask; register C is rejected.
 * nmi_mask(0/1): set mask and return previous value; 2 queries without change.
 * init establishes masked NMI before IDT activation, then enables it. */
void inthandle_dispatch(struct inthandle_frame *frame);
#endif
