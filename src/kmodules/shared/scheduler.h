#ifndef CLOUDBERRY_KMODULE_SCHEDULER_H
#define CLOUDBERRY_KMODULE_SCHEDULER_H
#include "module.h"
#include "diagnostics.h"
#include "inthandle.h"
#define SCHEDULER_REQUEST_ABI 3u
enum scheduler_entry_kind { SCHEDULER_METHOD=1, SCHEDULER_INTERRUPT=2 };
enum scheduler_task_state { SCHEDULER_QUEUED=1, SCHEDULER_RUNNING, SCHEDULER_COMPLETE, SCHEDULER_CANCELLED, SCHEDULER_WAITING };
/* Six register arguments, plus optional copied data. When bytes>0 the scheduler
 * copies data and substitutes its owned pointer into arguments[data_register].
 * Embedded pointers are NOT deep-copied. Other argument pointers remain borrowed.
 * Interrupt targets require a copied inthandle_frame, data_register=0 and zero
 * arguments. Its frame.vector must equal entry_vector. It is deferred dispatch,
 * not a CPU int instruction or a synthesized hardware interrupt. */
struct scheduler_payload {
    uintptr_t arguments[6];
    const void *data;
    size_t bytes;
    uint32_t data_register, reserved;
};
struct scheduler_request {
    uint32_t abi, kind;
    uint64_t process_id; /* Task owner; zero identifies kernel-owned work. */
    uint64_t deadline_ns; /* Absolute clock.now_ns; zero means ready immediately. */
    uintptr_t entry_vector; /* Mediated hub method address, or IRQ vector32..255. */
    uint16_t entry_major, entry_minor;
    uint32_t reserved;
    struct scheduler_payload payload;
};
struct scheduler_completion {
    uint64_t id, process_id;
    uint32_t state, kind;
    struct kernel_surface_result result;
};
struct scheduler_stats {
    uint64_t submitted, queued, running, complete, cancelled, retained, waiting;
};
/* Surfaces (uniform MODULE_ARGS):
 * init(hub,heap) -> 1; repeat same configuration is harmless.
 * submit(request) -> nonzero task ID; caller may release request/data on success.
 * run(budget) -> number dispatched, at most budget; nested run fails.
 * cancel(id) -> 1 for queued/waiting/already-cancelled tasks; running/complete fail.
 * result(id,out,reap) -> 1; peek any state, reap only complete/cancelled.
 * remove_process(process_id) -> number removed, including running work marked
 * for removal on return. Repeated removal of an empty process succeeds with zero.
 * Running work keeps its payload until return; same-process submissions fail
 * during deferred removal. Other process IDs are unaffected.
 * next_deadline(out uint64_t) -> IDLE=0, READY=1, DEADLINE=2; out set for 2.
 * stats(out) -> 1. queued includes ready and waiting; waiting is its subset. Retained tasks/payloads consume heap until explicitly reaped.
 * FIFO, synchronous run-to-completion on caller stack; callbacks are not threads. Deadlines promote work, not interrupt a callback.
 * Mediated queue operations share the recursive kernel lock. run releases it
 * across callback execution, retaining its running record until return.
 * No allocation from a hardware ISR. A callback uses its caller context; it
 * cannot block as a thread when invoked on the BSP dispatcher. */
struct kernel_surface_result scheduler_surface_init(MODULE_ARGS);
struct kernel_surface_result scheduler_surface_submit(MODULE_ARGS);
struct kernel_surface_result scheduler_surface_run(MODULE_ARGS);
struct kernel_surface_result scheduler_surface_cancel(MODULE_ARGS);
struct kernel_surface_result scheduler_surface_result(MODULE_ARGS);
struct kernel_surface_result scheduler_surface_remove_process(MODULE_ARGS);
enum scheduler_next { SCHEDULER_IDLE, SCHEDULER_READY, SCHEDULER_DEADLINE };
struct kernel_surface_result scheduler_surface_next_deadline(MODULE_ARGS);
struct kernel_surface_result scheduler_surface_stats(MODULE_ARGS);
/* info(out scheduler_diagnostics) -> 1; cumulative runtime/deadline diagnostics. */
struct kernel_surface_result scheduler_surface_info(MODULE_ARGS);
#endif
