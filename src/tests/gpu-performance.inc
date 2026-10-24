/* Disposable GPU/broker measurement window only. No candidate telemetry. */
#include "handles-profile.h"
#include "../interfaces/system.h"
#include "../kmodules/shared/clock.h"
struct gp_row {uint64_t calls,total_ns,max_ns,blocks;};
static unsigned gp_enabled,gp_clock_errors;
static struct gp_row gp_ops[CB_GPU_SELECTED+1],gp_commands[6];
static struct hp_report gp_broker;
static struct gpu_pair_state gp_bench;
static struct clock_info gp_clock_start,gp_clock_end;
static uint64_t gp_delta(uint64_t begin){uint64_t end=now();if(begin==UINT64_MAX || end==UINT64_MAX || end<begin){++gp_clock_errors;return 0;}return end-begin;}
static void gp_add(struct gp_row *r,uint64_t ns,uint64_t blocks){++r->calls;r->total_ns+=ns;if(ns>r->max_ns)r->max_ns=ns;r->blocks+=blocks;}
static void gp_issue(unsigned type,unsigned long start,unsigned blocks){
    if(!gp_enabled)return;
    unsigned i=type==VG_TRANSFER_TO_3D?0:type==VG_SUBMIT_3D?1:type==VG_SCANOUT?2:type==VG_FLUSH?3:type==VG_TRANSFER_FROM_3D?4:5;
    gp_add(gp_commands+i,gp_delta(start),blocks);
}
static void gp_io(const struct cb_io_gpu_request *q,const struct io_invocation *r,uint64_t start){
    if(!r || r->status || !q->operation || q->operation>CB_GPU_SELECTED){++gp_clock_errors;return;}
    gp_add(gp_ops+q->operation,gp_delta(start),0);
}
static int gp_clock_begin(void){kernel_surface_call info=kernel_surface_find(state.hub,"clock.info");return info && !CALL(info,&gp_clock_start,0,0).status;}
static int gp_clock_finish(void){kernel_surface_call info=kernel_surface_find(state.hub,"clock.info");return info && !CALL(info,&gp_clock_end,0,0).status && gp_clock_start.source==gp_clock_end.source && gp_clock_start.flags==gp_clock_end.flags && gp_clock_start.tsc_hz==gp_clock_end.tsc_hz;}
static void gp_field(const char *name,uint64_t value,unsigned first){if(!first)log_text(",");log_text("\"");log_text(name);log_text("\":");log_number(value);}
static void gp_record(const char *kind){log_text("GPU_PROFILE {\"kind\":\"");log_text(kind);log_text("\"");}
static void gp_row_log(const char *kind,unsigned id,const struct gp_row *r){gp_record(kind);gp_field("id",id,0);gp_field("calls",r->calls,0);gp_field("total_ns",r->total_ns,0);gp_field("max_ns",r->max_ns,0);gp_field("blocks",r->blocks,0);log_text("}\n");}
static int gp_dump(void){
    kernel_surface_call profile=kernel_surface_find(state.hub,"handles.profile");
    if(!profile){log_text("GPU_PROFILE FAIL missing_broker=1\n");return 0;}
    unsigned end_failed=CALL(profile,HP_END,&gp_broker,0).status;struct hp_status status={0};
    unsigned status_failed=CALL(profile,HP_STATUS,&status,0).status;
    gp_record("finalization");gp_field("end_failed",end_failed,0);gp_field("status_failed",status_failed,0);gp_field("active",status.active,0);gp_field("generation",status.generation,0);gp_field("gpu_clock_errors",gp_clock_errors,0);gp_field("broker_clock_errors",gp_broker.clock_errors,0);gp_field("allocation_failures",gp_broker.failures,0);gp_field("samples",gp_bench.bench_samples,0);log_text("}\n");
    gp_record("window");gp_field("samples",gp_bench.bench_samples,0);gp_field("total_ns",gp_bench.bench_ns,0);gp_field("max_ns",gp_bench.bench_max_ns,0);gp_field("clock_source",gp_clock_start.source,0);gp_field("clock_flags",gp_clock_start.flags,0);gp_field("tsc_hz",gp_clock_start.tsc_hz,0);gp_field("hpet_period_fs",gp_clock_start.hpet_period_fs,0);log_text("}\n");
    for(unsigned i=0;i<32;++i){gp_record("frame");gp_field("sample",i,0);gp_field("ns",gp_bench.bench_times[i],0);log_text("}\n");}
    for(unsigned i=1;i<=CB_GPU_SELECTED;++i)if(gp_ops[i].calls)gp_row_log("gpu_io",i,gp_ops+i);
    const unsigned commands[]={VG_TRANSFER_TO_3D,VG_SUBMIT_3D,VG_SCANOUT,VG_FLUSH,VG_TRANSFER_FROM_3D,0};
    for(unsigned i=0;i<6;++i)if(gp_commands[i].calls)gp_row_log("gpu_fence",commands[i],gp_commands+i);
    if(!end_failed){
    gp_record("heap");gp_field("allocations",gp_broker.allocations,0);gp_field("frees",gp_broker.frees,0);gp_field("failures",gp_broker.failures,0);gp_field("handoffs",gp_broker.handoffs,0);gp_field("queue_ns",gp_broker.queue_ns,0);gp_field("service_ns",gp_broker.service_ns,0);gp_field("resume_ns",gp_broker.resume_ns,0);log_text("}\n");
    for(unsigned i=0;i<HP_CPUS;++i){const struct hp_locks *r=gp_broker.locks+i;gp_record("lock");gp_field("cpu",i,0);gp_field("count",r->count,0);gp_field("wait_ns",r->wait_ns,0);gp_field("hold_ns",r->hold_ns,0);gp_field("max_wait_ns",r->max_wait_ns,0);gp_field("max_hold_ns",r->max_hold_ns,0);log_text("}\n");}
    for(unsigned i=0;i<gp_broker.actors;++i){const struct hp_row *r=&gp_broker.rows[i][CB_SYSTEM_INVOKE];gp_record("broker_invoke");gp_field("pid",gp_broker.pids[i],0);gp_field("count",r->count,0);gp_field("total_ns",r->total_ns,0);gp_field("max_ns",r->max_ns,0);const char *names[]={"check_ns","capture_ns","broker_ns","wait_ns","kernel_ns","output_ns"};for(unsigned j=0;j<HP_PHASES;++j)gp_field(names[j],r->phase_ns[j],0);log_text("}\n");}
    for(unsigned i=0;i<32;++i){gp_record("clock_overhead");gp_field("sample",i,0);gp_field("ns",gp_broker.overhead[i],0);log_text("}\n");}
    }
    if(end_failed || status_failed || gp_clock_errors || gp_broker.clock_errors || gp_broker.failures || gp_bench.bench_samples!=32){log_text("GPU_PROFILE FAIL finalization=1\n");return 0;}
    log_text("GPU_PROFILE PASS\n");return 1;
}
