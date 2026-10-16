#ifndef CLOUDBERRY_KMODULE_EVENT_H
#define CLOUDBERRY_KMODULE_EVENT_H
#include "module.h"
#include "diagnostics.h"
/* BSP dispatcher policy. init(hub); step(positive budget) dispatches at most one
 * preemptible context; run repeats indefinitely. Lazy production workers run
 * scheduler callbacks and IRQ draining separately, never on the event stack.
 * May HLT until an interrupt; success leaves IF enabled, entered failures clear
 * it, rejected calls preserve it. Recursive entry fails atomically. Budgets do
 * not impose hard time bounds on interrupt-disabled critical sections. */
struct kernel_surface_result event_surface_init(MODULE_ARGS);
struct kernel_surface_result event_surface_step(MODULE_ARGS);
struct kernel_surface_result event_surface_run(MODULE_ARGS);
struct kernel_surface_result event_surface_info(MODULE_ARGS);
#endif
