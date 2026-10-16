#ifndef CLOUDBERRY_CPU_H
#define CLOUDBERRY_CPU_H
#include "../kmodules/shared/module.h"
#include <stdbool.h>
#define CPU_TLB_VECTOR 225u
#define CPU_WAKE_VECTOR 226u
enum cpu_control_op {CPU_CURRENT,CPU_ENTER,CPU_LEAVE,CPU_DEPTH,CPU_CONFIGURE,CPU_WAKE,CPU_ACK,CPU_ACTIVE,CPU_STACK_BUSY,CPU_TIMER,CPU_ATTACH,CPU_METRICS,CPU_METRICS_RESET,CPU_IRQ_BEGIN,CPU_IRQ_END,CPU_LATENCY};
/* CPU_ENTER/LEAVE nest on one CPU and preserve IF. Never suspend with depth>0.
 * Domain 0 is global, domain 1 is clock; acquire global before clock.
 * Outer IF-enabled waiters keep IRQ delivery live, with thread switching
 * inhibited. A five-second HPET watchdog (plus iteration fallback) faults on
 * stalled acquisition/IPI/TLB waits. CPU_LATENCY reports this policy; 2 ms
 * critical notices are observations, not hard real-time enforcement.
 * IRQ callbacks must not enter these locks. Waiting CPUs service TLB mailboxes. */
struct cpu_metrics {
    uint64_t acquisitions,contended,wait_ticks,max_wait_ticks,hold_ticks,max_hold_ticks;
    uint64_t irq_off_ticks,max_irq_off_ticks,clock_regressions,max_hold_method,max_hold_domain;
    uint64_t irq_enabled_waits,max_wait_ns,max_hold_ns,max_irq_off_ns,critical_overruns;
    uint64_t domain_acquisitions[2],domain_contended[2],domain_wait_ticks[2],domain_hold_ticks[2];
};
struct cpu_record {
    uintptr_t self; /* GS:0; kernel reserves IA32_GS_BASE. */
    uint32_t lapic_id,index;
    uint64_t online,tlb_request,tlb_ack,depth;
    uintptr_t stack_top,stack_bottom,active_bottom,active_top,ist_bottom[3],ist_top[3];
    uint64_t timer_interrupts;
    struct cpu_metrics metrics;
    uint64_t lock_begin[2],irq_begin,lock_method[2],lock_depth[2],metrics_sequence,lock_hpet[2],irq_hpet;
};
struct cpu_config {struct cpu_record *records;size_t count;uintptr_t apic;uint32_t mode;uintptr_t watchdog_hpet;uint64_t hpet_period_fs;};
struct cpu_latency_policy {uint64_t wait_budget_ns,critical_notice_ns;uint32_t hardware_clock,reserved;};
struct kernel_surface_result kernel_cpu_control(MODULE_ARGS);
bool kernel_cpu_backend(const struct kernel_surfaces *);
uint64_t kernel_cpu_enter(void);
uint64_t kernel_cpu_enter_tag(uint64_t method);
uint64_t kernel_cpu_enter_domain(unsigned domain,uint64_t method);
void kernel_cpu_leave_domain(unsigned domain,uint64_t flags);
void kernel_cpu_leave(uint64_t flags);
void kernel_cpu_flush(void);
#endif
