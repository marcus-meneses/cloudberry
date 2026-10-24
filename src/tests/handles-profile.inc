/* Included only by the disposable broker variant, after broker declarations. */
#include "handles-profile.h"
struct hp_call {uint64_t begin,wait,kernel,check,capture;unsigned actor,operation;};
static struct {unsigned enabled;uint64_t live,active,generation;struct hp_report report;struct {unsigned depth;uint64_t hold;} cpu[HP_CPUS];} hp;
static int hp_enabled(void){return __atomic_load_n(&hp.enabled,__ATOMIC_ACQUIRE)!=0;}
static uint64_t hp_now(void){struct kernel_surface_result r=state.clock(0,0,0,0,0,0);if(r.status)__atomic_add_fetch(&hp.report.clock_errors,1,__ATOMIC_RELAXED);return r.value;}
static uint64_t hp_delta(uint64_t begin,uint64_t end){if(end<begin){__atomic_add_fetch(&hp.report.clock_errors,1,__ATOMIC_RELAXED);return 0;}return end-begin;}
struct hp_entry_stamp {uint64_t ns,generation;};
static struct hp_entry_stamp hp_stamp(void){
    uint64_t generation=__atomic_load_n(&hp.generation,__ATOMIC_ACQUIRE);
    if(!hp_enabled())return (struct hp_entry_stamp){0};
    uint64_t ns=hp_now();
    if(!hp_enabled() || generation!=__atomic_load_n(&hp.generation,__ATOMIC_ACQUIRE))return (struct hp_entry_stamp){0};
    return (struct hp_entry_stamp){ns,generation};
}
static void hp_alloc(int ok){if(ok)__atomic_add_fetch(&hp.live,1,__ATOMIC_RELAXED);if(hp_enabled())__atomic_add_fetch(ok?&hp.report.allocations:&hp.report.failures,1,__ATOMIC_RELAXED);}
static void hp_free(void){__atomic_sub_fetch(&hp.live,1,__ATOMIC_RELAXED);if(hp_enabled())__atomic_add_fetch(&hp.report.frees,1,__ATOMIC_RELAXED);}
static uint64_t hp_lock(void){
    if(!hp_enabled())return CALL(state.cpu,CPU_ENTER,0,0).value;
    uint64_t start=hp_now(),index=CALL(state.cpu,CPU_CURRENT,0,0).value;
    uint64_t flags=CALL(state.cpu,CPU_ENTER,0,0).value;
    if(hp_enabled() && index<HP_CPUS && !hp.cpu[index].depth++){
        uint64_t end=hp_now(),wait=hp_delta(start,end);struct hp_locks *m=&hp.report.locks[index];
        ++m->count;m->wait_ns+=wait;if(wait>m->max_wait_ns)m->max_wait_ns=wait;hp.cpu[index].hold=end;
    }return flags;
}
static void hp_unlock(uint64_t flags){
    if(hp_enabled()){uint64_t index=CALL(state.cpu,CPU_CURRENT,0,0).value;if(index<HP_CPUS && hp.cpu[index].depth && !--hp.cpu[index].depth){
        uint64_t hold=hp_delta(hp.cpu[index].hold,hp_now());struct hp_locks *m=&hp.report.locks[index];m->hold_ns+=hold;if(hold>m->max_hold_ns)m->max_hold_ns=hold;}}
    CALL(state.cpu,CPU_LEAVE,flags,0);
}
static int hp_begin(struct policy *owner,unsigned operation,struct hp_entry_stamp begin,struct hp_call *call){
    if(!begin.generation || operation>=HP_OPS)return 0;
    uint64_t flags=CALL(state.cpu,CPU_ENTER,0,0).value;int admitted=0;
    if(hp_enabled() && begin.generation==__atomic_load_n(&hp.generation,__ATOMIC_ACQUIRE)){
        for(unsigned i=0;i<hp.report.actors;++i)if(hp.report.pids[i]==owner->pid){*call=(struct hp_call){.begin=begin.ns,.check=hp_delta(begin.ns,hp_now()),.actor=i,.operation=operation};owner->probe=call;__atomic_add_fetch(&hp.active,1,__ATOMIC_RELEASE);admitted=1;break;}
    }
    CALL(state.cpu,CPU_LEAVE,flags,0);return admitted;
}
static void hp_finish(struct policy *owner,struct hp_call *call,uint64_t dispatch,uint64_t output){
    uint64_t end=hp_now(),total=hp_delta(call->begin,end),work=hp_delta(dispatch,output);
    struct hp_row *r=&hp.report.rows[call->actor][call->operation];++r->count;r->total_ns+=total;if(total>r->max_ns)r->max_ns=total;
    r->phase_ns[HP_CHECK]+=call->check;r->phase_ns[HP_CAPTURE]+=call->capture;
    r->phase_ns[HP_WAIT]+=call->wait;r->phase_ns[HP_KERNEL]+=call->kernel;
    if(work>=call->wait && work-call->wait>=call->kernel)r->phase_ns[HP_BROKER]+=work-call->wait-call->kernel;
    else ++hp.report.clock_errors;
    r->phase_ns[HP_OUTPUT]+=hp_delta(output,end);owner->probe=NULL;__atomic_sub_fetch(&hp.active,1,__ATOMIC_RELEASE);
}
static uint64_t hp_wait_begin(struct policy *owner){return owner->probe?hp_now():0;}
static void hp_wait_end(struct policy *owner,uint64_t start){if(owner->probe)owner->probe->wait+=hp_delta(start,hp_now());}
static void hp_handoff(struct request *r,unsigned point){
    if(!hp_enabled())return;
    uint64_t stamp=hp_now();
    if(point==0)r->queued_at=stamp;
    if(point==1)r->delivered_at=stamp;
    if(point==2)r->responded_at=stamp;
    if(point==3 && r->queued_at && r->delivered_at && r->responded_at){
        __atomic_add_fetch(&hp.report.handoffs,1,__ATOMIC_RELAXED);
        __atomic_add_fetch(&hp.report.queue_ns,hp_delta(r->queued_at,r->delivered_at),__ATOMIC_RELAXED);
        __atomic_add_fetch(&hp.report.service_ns,hp_delta(r->delivered_at,r->responded_at),__ATOMIC_RELAXED);
        __atomic_add_fetch(&hp.report.resume_ns,hp_delta(r->responded_at,stamp),__ATOMIC_RELAXED);
    }
}
struct kernel_surface_result handles_surface_profile(MODULE_ARGS){
    MODULE_UNUSED();if(!b)return module_result(0,0);
    if(a==HP_STATUS){*(struct hp_status *)b=(struct hp_status){hp_enabled(),__atomic_load_n(&hp.active,__ATOMIC_ACQUIRE),__atomic_load_n(&hp.generation,__ATOMIC_ACQUIRE)};return module_result(1,1);}
    if(a==HP_LIVE){*(uint64_t *)b=__atomic_load_n(&hp.live,__ATOMIC_RELAXED);return module_result(1,1);}
    if(a==HP_BEGIN){
        const struct hp_report *request=(const void *)b;uint64_t ids[HP_ACTORS];
        uint64_t flags=CALL(state.cpu,CPU_ENTER,0,0).value;int ok=0;
        if(hp_enabled() || __atomic_load_n(&hp.active,__ATOMIC_ACQUIRE) || hp.generation==UINT64_MAX || !request->actors || request->actors>HP_ACTORS)goto begin_done;
        uint64_t actors=request->actors;
        for(unsigned i=0;i<actors;++i){ids[i]=request->pids[i];if(!policy(ids[i]))goto begin_done;}
        hp.report=(struct hp_report){.actors=actors};
        for(unsigned i=0;i<actors;++i)hp.report.pids[i]=ids[i];
        for(unsigned i=0;i<32;++i){uint64_t start=hp_now();hp.report.overhead[i]=hp_delta(start,hp_now());}
        __atomic_add_fetch(&hp.generation,1,__ATOMIC_RELEASE);
        __atomic_store_n(&hp.enabled,1,__ATOMIC_RELEASE);ok=1;
begin_done:CALL(state.cpu,CPU_LEAVE,flags,0);return module_result(ok,ok);
    }
    if(a==HP_PAUSE){uint64_t flags=CALL(state.cpu,CPU_ENTER,0,0).value;__atomic_store_n(&hp.enabled,0,__ATOMIC_RELEASE);CALL(state.cpu,CPU_LEAVE,flags,0);return module_result(1,1);}
    if(a!=HP_END && a!=HP_READ)return module_result(0,0);
    uint64_t flags=CALL(state.cpu,CPU_ENTER,0,0).value;int ok=0;
    if(__atomic_load_n(&hp.active,__ATOMIC_ACQUIRE) || (a==HP_READ && hp_enabled()))goto read_done;
    if(a==HP_END)__atomic_store_n(&hp.enabled,0,__ATOMIC_RELEASE);
    hp.report.live_blocks=__atomic_load_n(&hp.live,__ATOMIC_RELAXED);*(struct hp_report *)b=hp.report;ok=1;
read_done:CALL(state.cpu,CPU_LEAVE,flags,0);return module_result(ok,ok);
}
#define HP_STAMP(name) struct hp_entry_stamp name=hp_stamp()
/* Pausing stops admission, not timestamps of probes already admitted. */
#define HP_ACTIVE_STAMP(name) uint64_t name=hp_active?hp_now():0
#define HP_BEGIN_CALL(owner,op,start) struct hp_call hp_call;int hp_active=hp_begin(owner,op,start,&hp_call)
#define HP_CAPTURE_DONE() do{if(hp_active)hp_call.capture=hp_delta(hp_call.begin+hp_call.check,hp_now());}while(0)
#define HP_FINISH(owner) do{if(hp_active)hp_finish(owner,&hp_call,hp_dispatch,hp_output);}while(0)
#define HP_WAIT_BEGIN(owner) uint64_t hp_wait_start=hp_wait_begin(owner)
#define HP_WAIT_END(owner) hp_wait_end(owner,hp_wait_start)
#define HP_HANDOFF(r,point) hp_handoff(r,point)
#define HP_KERNEL_BEGIN(owner) uint64_t hp_kernel_start=hp_wait_begin(owner)
#define HP_KERNEL_END(owner) do{if(owner->probe)owner->probe->kernel+=hp_delta(hp_kernel_start,hp_now());}while(0)
