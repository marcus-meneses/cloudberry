#ifndef CLOUDBERRY_EXECUTION_DIAGNOSTICS_H
#define CLOUDBERRY_EXECUTION_DIAGNOSTICS_H
#include <stdint.h>
struct scheduler_diagnostics {
    uint64_t completed,timed,timing_failures,clock_regressions,total_runtime_ns,max_runtime_ns;
    uint64_t last_task_id,last_process_id,late_jobs,total_lateness_ns,max_lateness_ns,last_late_task_id;
};
struct event_diagnostics {uint64_t steps,batches,jobs,idle_entries,wakes,notifications,failures;};
/* Cumulative BSP foreground counters, independent of result retention/reaping.
 * Saturating totals; runtime/lateness are monotonic-clock observations, not CPU
 * accounting. Timing failures never overwrite a callback's actual result. */
static inline void diagnostic_add(uint64_t *out,uint64_t amount) {*out=amount>UINT64_MAX-*out?UINT64_MAX:*out+amount;}
#endif
