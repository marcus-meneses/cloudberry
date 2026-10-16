#include "scheduler.h"
#include "diagnostics.h"
#include "../../kernel/cpu.h"
struct task {
    struct task *all_previous,*all_next,*ready_previous,*ready_next;
    uint64_t id,process_id,deadline_ns;
    uint32_t state,kind;
    int remove_on_return;
    uintptr_t entry_vector;
    kernel_surface_call entry;
    struct scheduler_payload payload;
    struct kernel_surface_result result;
    _Alignas(16) unsigned char data[];
};
static struct {
    const struct kernel_surfaces *hub;
    void *heap;
    kernel_surface_call allocate,release,interrupt_dispatch,interrupt_bound,now,cpu;
    struct task *all,*head,*tail,*wait_head,*wait_tail;
    uint64_t next_id;
    struct scheduler_stats stats;
    struct scheduler_diagnostics diagnostics;
    int initialized,dispatching;
} state;
static uint64_t lock(void) {return state.cpu?state.cpu(CPU_ENTER,0,0,0,0,0).value:0;}
static void unlock(uint64_t flags) {if(state.cpu)state.cpu(CPU_LEAVE,flags,0,0,0,0);}
static struct task *find(uint64_t id) {
    for (struct task *t=state.all;t;t=t->all_next) if (t->id==id) return t;
    return NULL;
}
static void unlink_pending(struct task *t) {
    int waiting=t->state==SCHEDULER_WAITING;
    struct task **head=waiting?&state.wait_head:&state.head,**tail=waiting?&state.wait_tail:&state.tail;
    if (t->ready_previous) t->ready_previous->ready_next=t->ready_next;else *head=t->ready_next;
    if (t->ready_next) t->ready_next->ready_previous=t->ready_previous;else *tail=t->ready_previous;
    t->ready_previous=t->ready_next=NULL;--state.stats.queued;
    if (waiting) --state.stats.waiting;
}
static void append_ready(struct task *t) {
    t->state=SCHEDULER_QUEUED;t->ready_previous=state.tail;t->ready_next=NULL;
    if (state.tail) state.tail->ready_next=t;else state.head=t;
    state.tail=t;++state.stats.queued;
}
static void insert_waiting(struct task *t) {
    struct task *after=state.wait_head;
    while (after && after->deadline_ns<=t->deadline_ns) after=after->ready_next;
    t->state=SCHEDULER_WAITING;t->ready_next=after;t->ready_previous=after?after->ready_previous:state.wait_tail;
    if (t->ready_previous) t->ready_previous->ready_next=t;else state.wait_head=t;
    if (after) after->ready_previous=t;else state.wait_tail=t;
    ++state.stats.queued;++state.stats.waiting;
}
static void promote_due(uint64_t now) {
    while (state.wait_head && state.wait_head->deadline_ns<=now) {
        struct task *t=state.wait_head;unlink_pending(t);append_ready(t);
    }
}
/* Only destroy records whose callback is no longer using their payload. */
static void destroy(struct task *t) {
    if (t->state==SCHEDULER_QUEUED || t->state==SCHEDULER_WAITING) unlink_pending(t);
    else if (t->state==SCHEDULER_COMPLETE) --state.stats.complete;
    else if (t->state==SCHEDULER_CANCELLED) --state.stats.cancelled;
    if (t->all_previous) t->all_previous->all_next=t->all_next; else state.all=t->all_next;
    if (t->all_next) t->all_next->all_previous=t->all_previous;
    --state.stats.retained;
    state.release((uintptr_t)state.heap,(uintptr_t)t,0,0,0,0);
}
struct kernel_surface_result scheduler_surface_init(MODULE_ARGS) {
    MODULE_UNUSED();
    const struct kernel_surfaces *hub=(const void *)a;
    if (state.initialized) return module_result(1,state.hub==hub && state.heap==(void *)b);
    if (!hub || hub->magic!=KERNEL_SURFACES_MAGIC || hub->major!=2 || !b) return module_result(0,0);
    kernel_surface_call allocate=kernel_surface_find_version(hub,"mem.calloc",1,0);
    kernel_surface_call release=kernel_surface_find_version(hub,"mem.free",1,0);
    kernel_surface_call dispatch=kernel_surface_find_version(hub,"inthandle.dispatch",1,1);
    kernel_surface_call bound=kernel_surface_find_version(hub,"inthandle.is_bound",1,0);
    kernel_surface_call now=kernel_surface_find_version(hub,"clock.now_ns",1,0);
    if (!allocate || !release || !dispatch || !bound || !now) return module_result(0,0);
    state.cpu=kernel_surface_find_version(hub,"cpu.control",1,0);
    state.hub=hub; state.heap=(void *)b; state.allocate=allocate; state.release=release;
    state.interrupt_dispatch=dispatch; state.interrupt_bound=bound;state.now=now;
    state.next_id=1; state.initialized=1;
    return module_result(1,1);
}
struct kernel_surface_result scheduler_surface_submit(MODULE_ARGS) {
    MODULE_UNUSED();
    if (!state.initialized || !a || !state.next_id) return module_result(0,0);
    const struct scheduler_request *r=(const void *)a;
    if (r->abi!=SCHEDULER_REQUEST_ABI || r->reserved || r->payload.reserved || !r->entry_major ||
        r->payload.bytes>SIZE_MAX-sizeof(struct task) ||
        (r->payload.bytes && (!r->payload.data || r->payload.data_register>=6))) return module_result(0,0);
    for (struct task *t=state.all;t;t=t->all_next)
        if (t->process_id==r->process_id && t->remove_on_return) return module_result(0,0);
    kernel_surface_call entry=NULL;
    if (r->kind==SCHEDULER_METHOD) {
        const struct kernel_surface *entries=kernel_surface_entries(state.hub);
        for (size_t i=0;i<state.hub->count;++i)
            if ((uintptr_t)entries[i].call==r->entry_vector && entries[i].major==r->entry_major &&
                entries[i].minor>=r->entry_minor) { entry=entries[i].call; break; }
        if (!entry) return module_result(0,0);
    } else if (r->kind==SCHEDULER_INTERRUPT) {
        if (r->entry_vector<32 || r->entry_vector>255 || r->entry_major!=1 || r->entry_minor ||
            r->payload.bytes!=sizeof(struct inthandle_frame) || r->payload.data_register ||
            ((const struct inthandle_frame *)r->payload.data)->vector!=r->entry_vector) return module_result(0,0);
        for (size_t i=0;i<6;++i) if (r->payload.arguments[i]) return module_result(0,0);
        struct kernel_surface_result bound=state.interrupt_bound(r->entry_vector,0,0,0,0,0);
        if (bound.status || !bound.value) return module_result(0,0);
    } else return module_result(0,0);
    int waiting=0;
    if (r->deadline_ns) {
        struct kernel_surface_result now=state.now(0,0,0,0,0,0);
        if (now.status) return module_result(0,0);
        waiting=r->deadline_ns>now.value;
    }
    struct kernel_surface_result memory=state.allocate((uintptr_t)state.heap,1,sizeof(struct task)+r->payload.bytes,0,0,0);
    if (memory.status || !memory.value) return module_result(0,0);
    struct task *t=(void *)memory.value;
    t->id=state.next_id++; t->process_id=r->process_id;t->deadline_ns=r->deadline_ns; t->state=SCHEDULER_QUEUED; t->kind=r->kind;
    t->entry_vector=r->entry_vector; t->entry=entry; t->payload=r->payload;
    if (r->payload.bytes) {
        const unsigned char *source=r->payload.data;
        for (size_t i=0;i<r->payload.bytes;++i) t->data[i]=source[i];
        t->payload.data=t->data;
        t->payload.arguments[t->payload.data_register]=(uintptr_t)t->data;
    } else t->payload.data=NULL;
    t->all_next=state.all;
    if (state.all) state.all->all_previous=t;
    state.all=t;
    if (waiting) insert_waiting(t);else append_ready(t);
    ++state.stats.submitted; ++state.stats.retained;
    if(state.cpu && state.cpu(CPU_CURRENT,0,0,0,0,0).value)state.cpu(CPU_WAKE,0,0,0,0,0);
    return module_result(t->id,1);
}
struct kernel_surface_result scheduler_surface_run(MODULE_ARGS) {
    MODULE_UNUSED();
    if(state.cpu && state.cpu(CPU_DEPTH,0,0,0,0,0).value)return module_result(0,0);
    uint64_t flags=lock();
    if (!state.initialized || state.dispatching) {unlock(flags);return module_result(0,0);}
    state.dispatching=1;
    size_t count=0;
    while (count<a) {
        struct kernel_surface_result now=state.now(0,0,0,0,0,0);
        if (now.status) {state.dispatching=0;unlock(flags);return module_result(count,0);}
        promote_due(now.value);
        if (!state.head) break;
        struct task *t=state.head; unlink_pending(t);
        t->state=SCHEDULER_RUNNING; state.stats.running=1;
        struct kernel_surface_result started=state.now(0,0,0,0,0,0);
        uint64_t dispatch_ns=started.status?now.value:started.value;
        if (t->deadline_ns && dispatch_ns>t->deadline_ns) {
            uint64_t late=dispatch_ns-t->deadline_ns;
            diagnostic_add(&state.diagnostics.late_jobs,1);diagnostic_add(&state.diagnostics.total_lateness_ns,late);
            if (late>state.diagnostics.max_lateness_ns) state.diagnostics.max_lateness_ns=late;
            state.diagnostics.last_late_task_id=t->id;
        }
        unlock(flags);
        struct kernel_surface_result outcome;
        if (t->kind==SCHEDULER_METHOD) {
            uintptr_t *p=t->payload.arguments;
            outcome=t->entry(p[0],p[1],p[2],p[3],p[4],p[5]);
        } else {
            struct kernel_surface_result bound=state.interrupt_bound(t->entry_vector,0,0,0,0,0);
            outcome=bound.status || !bound.value ? module_result(0,0) :
                state.interrupt_dispatch((uintptr_t)t->payload.data,0,0,0,0,0);
        }
        struct kernel_surface_result ended=state.now(0,0,0,0,0,0);
        flags=lock();t->result=outcome;
        diagnostic_add(&state.diagnostics.completed,1);
        state.diagnostics.last_task_id=t->id;state.diagnostics.last_process_id=t->process_id;
        if (started.status || ended.status || ended.value<started.value) {
            diagnostic_add(&state.diagnostics.timing_failures,1);
            if (!started.status && !ended.status && ended.value<started.value) diagnostic_add(&state.diagnostics.clock_regressions,1);
        } else {
            uint64_t duration=ended.value-started.value;diagnostic_add(&state.diagnostics.timed,1);
            diagnostic_add(&state.diagnostics.total_runtime_ns,duration);
            if (duration>state.diagnostics.max_runtime_ns) state.diagnostics.max_runtime_ns=duration;
        }
        state.stats.running=0;
        if (t->remove_on_return) destroy(t);
        else { t->state=SCHEDULER_COMPLETE; ++state.stats.complete; }
        ++count;
    }
    state.dispatching=0;unlock(flags);
    return module_result(count,1);
}
struct kernel_surface_result scheduler_surface_cancel(MODULE_ARGS) {
    MODULE_UNUSED(); if (!state.initialized) return module_result(0,0);
    struct task *t=find(a);
    if (!t) return module_result(0,0);
    if (t->state==SCHEDULER_CANCELLED) return module_result(1,1);
    if (t->state!=SCHEDULER_QUEUED && t->state!=SCHEDULER_WAITING) return module_result(0,0);
    unlink_pending(t); t->state=SCHEDULER_CANCELLED; t->result=module_result(0,0);
    ++state.stats.cancelled; return module_result(1,1);
}
struct kernel_surface_result scheduler_surface_result(MODULE_ARGS) {
    MODULE_UNUSED(); if (!state.initialized || !b || c>1) return module_result(0,0);
    struct task *t=find(a);
    if (!t || (c && t->state!=SCHEDULER_COMPLETE && t->state!=SCHEDULER_CANCELLED)) return module_result(0,0);
    struct scheduler_completion *out=(void *)b;
    *out=(struct scheduler_completion){t->id,t->process_id,t->state,t->kind,t->result};
    if (c) destroy(t);
    return module_result(1,1);
}
struct kernel_surface_result scheduler_surface_remove_process(MODULE_ARGS) {
    MODULE_UNUSED(); if (!state.initialized) return module_result(0,0);
    size_t removed=0;
    for (struct task *t=state.all,*next;t;t=next) {
        next=t->all_next;
        if (t->process_id!=a) continue;
        ++removed;
        if (t->state==SCHEDULER_RUNNING) t->remove_on_return=1;
        else destroy(t);
    }
    return module_result(removed,1);
}
struct kernel_surface_result scheduler_surface_stats(MODULE_ARGS) {
    MODULE_UNUSED(); if (!state.initialized || !a) return module_result(0,0);
    *(struct scheduler_stats *)a=state.stats; return module_result(1,1);
}

struct kernel_surface_result scheduler_surface_next_deadline(MODULE_ARGS) {
    MODULE_UNUSED();if (!state.initialized || !a) return module_result(0,0);
    struct kernel_surface_result now=state.now(0,0,0,0,0,0);
    if (now.status) return module_result(0,0);
    promote_due(now.value);
    *(uint64_t *)a=state.wait_head?state.wait_head->deadline_ns:0;
    return module_result(state.head?SCHEDULER_READY:state.wait_head?SCHEDULER_DEADLINE:SCHEDULER_IDLE,1);
}

struct kernel_surface_result scheduler_surface_info(MODULE_ARGS) {
    MODULE_UNUSED();if (!state.initialized || !a) return module_result(0,0);
    *(struct scheduler_diagnostics *)a=state.diagnostics;return module_result(1,1);
}
