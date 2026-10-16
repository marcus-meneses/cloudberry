#ifndef CLOUDBERRY_IRQ_H
#define CLOUDBERRY_IRQ_H
#include "module.h"
enum irq_kind {IRQ_IOAPIC=1,IRQ_MSI=2};
struct irq_request {
    uint32_t kind,gsi,level,active_low;
    uint64_t owner;
    kernel_surface_call callback;
    uintptr_t context;
    uint16_t major,minor;
    uint32_t reserved;
};
struct irq_route {uint64_t id,owner;uint32_t kind,vector,gsi,masked;uint64_t message_address;uint32_t message_data,reserved;};
struct irq_notification {uint64_t id,owner,count;uint32_t vector,gsi,kind,reserved;};
struct irq_info {struct irq_route route;uint64_t delivered,pending,drained,failures,overflow,total_defer_ticks,max_defer_ticks;};
/* BSP foreground ownership: init(hub,heap), route(request,out) initially masked,
 * enable(id,owner,enable), release(id,owner,hardware_quiesced), info(id,out).
 * MSI/MSI-X owners program/mask their device; this module returns the message.
 * IOAPIC callers supply resolved GSI/polarity/trigger (no AML/ISA translation).
 * ISR masks level input, coalesces count, EOIs; drain(budget) calls registered
 * callback(notification,context) in foreground. Level callback clears device
 * cause and explicitly reenables. No allocation or foreground lock in ISR. */
/* context() -> 1 only on the current deferred callback thread; blocking device
 * operations must reject this context to avoid waiting on their own dispatcher. */
struct kernel_surface_result irq_surface_context(MODULE_ARGS);
struct kernel_surface_result irq_surface_init(MODULE_ARGS);
struct kernel_surface_result irq_surface_route(MODULE_ARGS);
struct kernel_surface_result irq_surface_enable(MODULE_ARGS);
struct kernel_surface_result irq_surface_release(MODULE_ARGS);
struct kernel_surface_result irq_surface_info(MODULE_ARGS);
struct kernel_surface_result irq_surface_pending(MODULE_ARGS);
struct kernel_surface_result irq_surface_drain(MODULE_ARGS);
struct kernel_surface_result irq_surface_interrupt(MODULE_ARGS);
#endif
