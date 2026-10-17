#ifndef CLOUDBERRY_CPU_H
#define CLOUDBERRY_CPU_H
#include "../kmodules/shared/module.h"
#include <stdbool.h>
#include "cpu-profile.h"
#define CPU_TLB_VECTOR 225u
#define CPU_WAKE_VECTOR 226u
enum cpu_control_op {CPU_CURRENT,CPU_ENTER,CPU_LEAVE,CPU_DEPTH,CPU_CONFIGURE,CPU_WAKE,CPU_ACK,CPU_ACTIVE,CPU_STACK_BUSY,CPU_TIMER,CPU_ATTACH,CPU_METRICS,CPU_METRICS_RESET,CPU_IRQ_BEGIN,CPU_IRQ_END,CPU_LATENCY,CPU_PROFILE_CLOCK};
/* CPU_ENTER/LEAVE nest on one CPU and preserve IF. Never suspend with depth>0.
 * Domain 0 is global, domain 1 is clock; acquire global before clock.
 * Outer IF-enabled waiters keep IRQ delivery live, with thread switching
 * inhibited. A five-second HPET watchdog (plus iteration fallback) faults on
 * stalled acquisition/IPI/TLB waits. CPU_LATENCY reports this policy; 2 ms
 * critical notices are observations, not hard real-time enforcement.
 * IRQ callbacks must not enter these locks. Waiting CPUs service TLB mailboxes. */
/* CPU_PROFILE_CLOCK requires held domain 1. b=0 invalidates statistics' TSC
 * rate, b=qualified Hz publishes a fresh epoch, b=UINT64_MAX queries whether a
 * profiling-observed TSC regression invalidates that epoch. Only clock policy
 * publishes rates. Watchdog deadlines always use independent HPET. */
struct cpu_metrics {
    uint64_t acquisitions,contended,wait_ticks,max_wait_ticks,hold_ticks,max_hold_ticks;
    uint64_t irq_off_ticks,max_irq_off_ticks,clock_regressions,max_hold_method,max_hold_domain;
    uint64_t irq_enabled_waits,max_wait_ns,max_hold_ns,max_irq_off_ns,critical_overruns;
    uint64_t domain_acquisitions[2],domain_contended[2],domain_wait_ticks[2],domain_hold_ticks[2];
    uint64_t tsc_ns_samples,hpet_ns_samples,unavailable_ns_samples;
    struct cpu_profile_peak wait_peak,hold_peak,irq_peak;
};
struct cpu_record {
    uintptr_t self; /* GS:0; kernel reserves IA32_GS_BASE. */
    uint32_t lapic_id,index;
    uint64_t online,tlb_request,tlb_ack,depth,address_space;
    uintptr_t stack_top,stack_bottom,active_bottom,active_top,ist_bottom[3],ist_top[3];
    uint64_t timer_interrupts;
    struct cpu_metrics metrics;
    uint64_t lock_method[2],lock_depth[2],metrics_sequence,lock_caller[2],irq_method,irq_domain,irq_caller;
    struct cpu_profile_stamp lock_stamp[2],irq_stamp;
    uint64_t profile_fault_epoch,metrics_depth;
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
/* Space-specific flush requires the global CPU lock. ID0 flushes shared kernel
 * translations on every CPU; other IDs target CPUs currently using that root. */
void kernel_cpu_flush_space(uint64_t id);
uint64_t kernel_cpu_space(void);
void kernel_cpu_set_space(uint64_t id);
void kernel_cpu_select_root(uint64_t root);
#endif
