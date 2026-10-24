/* Disposable test-module replacement only. Production exports no admission
 * override. Fault-mode release verifies rollback; ordinary fixture release
 * still fails closed. No readiness/release bypass enters candidate images. */
#ifndef GPU_PAIR_PROFILE
#define GPU_PAIR_PROFILE 0
#endif
#if GPU_PAIR_PROFILE
static void gp_issue(unsigned,unsigned long,unsigned);
#define GPU_PROFILE_ISSUE(type,start,blocks) gp_issue(type,start,blocks)
#endif
#include "../kmodules/shared/gpu-virtio.c"
#include "../kmodules/shared/render-selection.h"
#include "../kmodules/shared/handles.h"
#include "../kmodules/shared/process.h"
#include "../kmodules/shared/applications.h"
#include "../kmodules/shared/bootstrap.h"
#include "../interfaces/startup.h"
#include "gpu-pair.h"
#if GPU_PAIR_PROFILE
#include "gpu-pair-profile-address.h"
#else
#include "gpu-pair-address.h"
#endif
static struct {uint64_t sequence,captures;uint32_t *pixels;size_t bytes;uint32_t width,height,error;} snapshot;
#ifndef GPU_PAIR_STARTUP_FAULT
#define GPU_PAIR_STARTUP_FAULT 0
#endif
#define GPU_PAIR_RENDERING (GPU_PAIR_ACCEL && !GPU_PAIR_STARTUP_FAULT)
static struct {uint64_t display,failed,code;unsigned release_verified;} startup_fault;
static uint64_t observer_thread;
static void log_text(const char *s){size_t n=0;while(s[n])++n;kernel_surface_call fn=kernel_surface_find(state.hub,"hardware.log");if(fn)CALL(fn,s,n,0);}
static void log_number(uint64_t n){char text[21];unsigned at=20;text[at]=0;do{text[--at]=(char)('0'+n%10);n/=10;}while(n);log_text(text+at);}
static int wait_tick(void){uint64_t t=now();return t!=UINT64_MAX && t<UINT64_MAX-1000000 && !CALL(state.thread,THREAD_BLOCK_UNTIL,t+1000000,0).status;}
#if GPU_PAIR_PROFILE
#include "gpu-performance.inc"
#endif
/* Capture in the display's own syscall continuation, after PRESENT. The
 * observer never competes for the GPU lock with a provider's next command. */
struct kernel_surface_result gpu_pair_io(MODULE_ARGS){
    struct io_invocation *r=(void *)a;struct cb_io_gpu_request q={0};
    if(r && r->operation==CB_IO_GPU && r->input && r->bytes>=sizeof(q))copy(&q,r->input,sizeof(q));
#if GPU_PAIR_PROFILE
    uint64_t profile_start=gp_enabled?now():0;
#endif
    struct kernel_surface_result result=gpu_virtio_io(a,b,c,d,e,f);
#if GPU_PAIR_PROFILE
    if(gp_enabled){gp_io(&q,r,profile_start);return result;}
#endif
    if(GPU_PAIR_STARTUP_FAULT && !result.status && r && !r->status && q.operation==CB_GPU_OPEN)startup_fault.display=r->process;
    if(!GPU_PAIR_RENDERING || result.status || !r || r->status || q.operation!=CB_GPU_PRESENT)return result;
    __atomic_add_fetch(&snapshot.sequence,1,__ATOMIC_ACQ_REL);
    struct controller *s=instance(q.device);struct resource *image=s?resource(s,q.resource):NULL;
    if(!image || !image->public.pixels || gpu_virtio_readback(q.device,q.resource,0,0,0,0).status)snapshot.error=1;
    else {
        size_t bytes=image->public.bytes;
        if(snapshot.pixels && snapshot.bytes!=bytes)snapshot.error=2;
        if(!snapshot.pixels){snapshot.pixels=allocate(bytes);snapshot.bytes=bytes;}
        if(!snapshot.pixels)snapshot.error=3;
        if(!snapshot.error){copy(snapshot.pixels,(const void *)image->public.pixels,bytes);snapshot.width=image->public.width;snapshot.height=image->public.height;++snapshot.captures;}
    }
    __atomic_add_fetch(&snapshot.sequence,1,__ATOMIC_RELEASE);return result;
}
struct kernel_surface_result gpu_pair_exit(MODULE_ARGS){
    if(GPU_PAIR_STARTUP_FAULT){
        kernel_surface_call info=kernel_surface_find(state.hub,"process.info");struct process_info p;
        if(info && !CALL(info,a,&p,0).status && p.exit_code>=201 && p.exit_code<=204){
            startup_fault.failed=a;startup_fault.code=p.exit_code;
        }
    }
    return gpu_virtio_exit(a,b,c,d,e,f);
}
struct kernel_surface_result gpu_pair_discover(MODULE_ARGS){
    struct kernel_surface_result result=gpu_virtio_discover(a,b,c,d,e,f);if(result.status || observer_thread)return result;
    kernel_surface_call create=kernel_surface_find(state.hub,"threads.create"),entry=kernel_surface_find(state.hub,"gpu.pair.observe");
    struct thread_request request={.abi=1,.cpu=0,.process_id=0,.stack_bytes=65536,.entry=(uintptr_t)entry,.major=1};
    struct kernel_surface_result made=create&&entry?CALL(create,&request,0,0):module_result(0,0);
    if(made.status){log_text("GPU_PAIR FAIL observer startup\n");return made;}observer_thread=made.value;return result;
}
struct kernel_surface_result gpu_pair_probe(MODULE_ARGS){
    MODULE_UNUSED();struct render_probe *p=(void *)a;
    if(!GPU_PAIR_ACCEL || !p || p->bytes!=sizeof(*p) || p->major!=1 || p->minor || p->reserved || !hardware(p->hardware_index))return module_result(0,0);
    struct kernel_surface_result result=gpu_virtio_probe(a,b,c,d,e,f);
    struct controller *s=hardware(p->hardware_index);
    log_text("GPU_PAIR PRODUCTION PROBE stage=");log_number(s->info->stage);log_text(" vga=");log_number(s->boot_vga[0]);log_text("/");log_number(s->boot_vga[1]);log_text("/");log_number(s->boot_vga[2]);log_text("/");log_number(s->boot_vga[3]);log_text("/");log_number(s->boot_vga[4]);log_text("\n");
    if(!result.status && p->capabilities==RENDER_GPU_COMPLETE){log_text("GPU_PAIR PRODUCTION PROBE verified=1\n");return result;}
    /* Only non-GL fault references may force a cold, firmware-matched 2D
     * transport to OPEN. Native GL acceptance always uses production policy. */
    if(GPU_PAIR_STARTUP_FAULT && !result.status && s->info->stage==103 && !(s->info->offered&1) && (s->info->offered&(UINT64_C(1)<<32))){state.selected=s;p->capabilities=RENDER_GPU_COMPLETE;return module_result(1,1);}
    return result;
}
struct kernel_surface_result gpu_pair_release(MODULE_ARGS){
    MODULE_UNUSED();
    if(!GPU_PAIR_STARTUP_FAULT)return gpu_virtio_release(a,b,c,d,e,f);
    kernel_surface_call info=kernel_surface_find(state.hub,"process.info"),inspect=kernel_surface_find(state.hub,"handles.inspect");
    struct process_info process;struct handle_publication_info publication;
    struct controller *s=hardware(a);
    if(!s || !info || !inspect || !startup_fault.display || !startup_fault.failed || !CALL(info,startup_fault.display,&process,0).status || !CALL(info,startup_fault.failed,&process,0).status)goto rejected;
    const char *names[]={"/@display","/@display.render","/@graphics","/@graphics-overlay"};
    for(unsigned i=0;i<4;++i)if(!CALL(inspect,names[i],&publication,0).status)goto rejected;
    if(s->owned || s->owner_pinned || gpu_bridge_owner(s) || s->resources || s->contexts || s->attachments || s->dma.id || s->route.id || s->info->state!=GPU_CLOSED)goto rejected;
    if(s->qualified){if(gpu_virtio_release(a,b,c,d,e,f).status)goto rejected;log_text("GPU_PAIR PRODUCTION RELEASE verified=1\n");}
    startup_fault.release_verified=1;
    log_text(startup_fault.code==204?"GPU_PAIR STARTUP REJECTED":"GPU_PAIR STARTUP FALLBACK READY");
    log_text(" code=");log_number(startup_fault.code);log_text(" display=");log_number(startup_fault.display);log_text(" failed=");log_number(startup_fault.failed);log_text("\n");
    /* Negative case: even with actual hardware cleanup complete, a release
     * contract that reports uncertainty must prevent fallback and app launch. */
    return module_result(1,startup_fault.code!=204);
rejected:
    log_text("GPU_PAIR FAIL startup rollback did not quiesce\n");return module_result(0,0);
}
static int pixel(unsigned x,unsigned y,uint32_t expected){return x<snapshot.width && y<snapshot.height && (snapshot.pixels[(size_t)y*snapshot.width+x]&0xffffff)==expected;}
static int image_matches(unsigned phase,const struct gpu_pair_state *s){
    uint64_t sequence=__atomic_load_n(&snapshot.sequence,__ATOMIC_ACQUIRE);if(sequence&1 || !snapshot.pixels || snapshot.error || snapshot.width!=s->width || snapshot.height!=s->height)return 0;
    int ok=pixel(8,8,0x102030);
    if(phase==1){ok=ok&&pixel(20,20,0xff0000)&&pixel(48,20,0x00ff00)&&pixel(20,48,0x0000ff)&&pixel(48,48,0xffffff)&&pixel(75,20,0x102030);}
    else if(phase!=8){ok=ok&&pixel(44,28,0xff0000)&&pixel(72,28,0x00ff00)&&pixel(44,56,0x0000ff)&&pixel(72,56,0xffffff)&&pixel(20,20,0x102030);}
    else ok=ok&&pixel(44,28,0x102030)&&pixel(72,28,0x102030)&&pixel(100,52,0x102030);
    if(phase!=8){unsigned white=0,orange=0,top=phase==1?16:50;for(unsigned y=top;y<top+24;++y)for(unsigned x=96;x<288;++x){uint32_t p=snapshot.pixels[(size_t)y*snapshot.width+x],r=(p>>16)&255,g=(p>>8)&255,b=p&255;if(r>50&&r==g&&g==b)++white;if(r>g&&g>b&&r>50)++orange;}ok=ok&&white>=10&&orange>=5;}
    if(phase==4 || phase==8)ok=ok&&pixel(s->x,s->y,0x101010)&&pixel(s->x+1,s->y+2,0xfafafa)&&pixel(s->x+10,s->y+2,0x102030);
    if(phase==5)ok=ok&&pixel(s->x,s->y,0x102030)&&pixel(s->x+1,s->y+2,0x102030);
    return ok && sequence==__atomic_load_n(&snapshot.sequence,__ATOMIC_ACQUIRE);
}
#include "gpu-pair-restore.inc"
static void phase_log(unsigned phase,const struct gpu_pair_state *s){log_text("GPU_PAIR PHASE ");log_number(phase);log_text(" width=");log_number(s->width);log_text(" height=");log_number(s->height);log_text(" x=");log_number(s->x);log_text(" y=");log_number(s->y);log_text("\n");}
struct kernel_surface_result gpu_pair_observe(MODULE_ARGS){
    MODULE_UNUSED();kernel_surface_call inspect=kernel_surface_find(state.hub,"handles.inspect"),pcopy=kernel_surface_find(state.hub,"process.copy"),pinfo=kernel_surface_find(state.hub,"process.info"),appinfo=kernel_surface_find(state.hub,"applications.info");
    struct handle_publication_info client={0},display={0},graphics={0},overlay={0},backend={0};struct gpu_pair_state shared={0};struct applications_info baseline={0};unsigned phase=0;uint64_t deadline=now()+UINT64_C(120000000000);
    if(!inspect || !pcopy || !pinfo || !appinfo)goto failed;
    while(CALL(inspect,GPU_PAIR_NAME,&client,0).status){if(now()>=deadline || !wait_tick())goto failed;}
    if(!client.process || CALL(inspect,"/@display",&display,0).status || CALL(inspect,"/@graphics",&graphics,0).status || CALL(inspect,"/@graphics-overlay",&overlay,0).status || overlay.process!=graphics.process)goto failed;
    struct process_info di,gi,ci;
    if(CALL(pinfo,display.process,&di,0).status || CALL(pinfo,graphics.process,&gi,0).status || CALL(pinfo,client.process,&ci,0).status || !di.address_space || !gi.address_space || !ci.address_space || di.address_space==gi.address_space || di.address_space==ci.address_space || gi.address_space==ci.address_space)goto failed;
    if(GPU_PAIR_RENDERING && (CALL(inspect,"/@display.render",&backend,0).status || backend.process!=display.process))goto failed;
    if(GPU_PAIR_STARTUP_FAULT){
        const struct kernel_hub_table *table=kernel_hub_find_table(state.hub,"graphics.backend");
        const struct render_selection *selected=table && table->bytes==sizeof(struct render_selection)?(const void *)table->address:NULL;
        if(!startup_fault.release_verified || !selected || selected->state!=RENDER_ACTIVE || selected->backend!=RENDER_VGA || selected->reason!=RENDER_START_FAILED)goto failed;
    }
    if(CALL(appinfo,&baseline,0,0).status)goto failed;
    log_text("GPU_PAIR PROVIDERS display=");log_number(display.process);log_text(" graphics=");log_number(graphics.process);log_text(" client=");log_number(client.process);log_text(" separate_spaces=1\n");
    for(phase=0;phase<=7;++phase){
        deadline=now()+UINT64_C(60000000000);
        for(;;){
            if(pcopy(client.process,GPU_PAIR_STATE_ADDRESS,(uintptr_t)&shared,sizeof(shared),0,0).status || shared.magic!=GPU_PAIR_MAGIC || shared.error || snapshot.error || now()>=deadline)goto failed;
#if GPU_PAIR_PROFILE
            if(phase==2 && shared.phase==11 && gp_enabled){
                gp_enabled=0;kernel_surface_call profile=kernel_surface_find(state.hub,"handles.profile");
                if(!profile || CALL(profile,HP_PAUSE,&gp_broker,0).status || shared.bench_samples!=32 || !gp_clock_finish())goto failed;
                gp_bench=shared;uint64_t ack=12;if(pcopy(client.process,GPU_PAIR_STATE_ADDRESS+16,(uintptr_t)&ack,8,1,0).status)goto failed;
            }
#endif
            if(shared.phase==phase)break;
            if(!wait_tick())goto failed;
        }

        if(phase==7 && shared.key!='a')goto failed;
        if(phase==1 || phase==2 || phase==4 || phase==5){
            while(GPU_PAIR_RENDERING && !image_matches(phase,&shared)){if(snapshot.error || now()>=deadline || !wait_tick())goto failed;}
        }
        phase_log(phase,&shared);
        if(phase==3)log_text("GPU_PAIR MOUSE WAIT\n");
        if(phase==6)log_text("GPU_PAIR KEY WAIT\n");
        /* A bounded host screenshot window; never a kernel busy wait. */
        uint64_t hold=now()+UINT64_C(1000000000);while(now()<hold)if(!wait_tick())goto failed;
#if GPU_PAIR_PROFILE
        if(phase==1){
            kernel_surface_call profile=kernel_surface_find(state.hub,"handles.profile");gp_broker=(struct hp_report){.actors=3,.pids={client.process,graphics.process,display.process}};
            if(!profile || CALL(profile,HP_BEGIN,&gp_broker,0).status || !gp_clock_begin())goto failed;
            gp_enabled=1;
        }
#endif
        uint64_t ack=phase+1;if(pcopy(client.process,GPU_PAIR_STATE_ADDRESS+16,(uintptr_t)&ack,8,1,0).status)goto failed;
    }
    phase=8;deadline=now()+UINT64_C(30000000000);
    for(;;){struct applications_info info={0};if(CALL(appinfo,&info,0,0).status || info.faulted!=baseline.faulted || (info.reaped>baseline.reaped && info.last_exit))goto failed;
        int gone=CALL(inspect,GPU_PAIR_NAME,&client,0).status!=0;
        if(gone && info.reaped>baseline.reaped && (!GPU_PAIR_RENDERING || image_matches(8,&shared)))break;
        if(now()>=deadline || !wait_tick())goto failed;
    }
    phase_log(8,&shared);uint64_t hold=now()+UINT64_C(1000000000);while(now()<hold)if(!wait_tick())goto failed;
    if(CALL(inspect,"/@graphics",&graphics,0).status)goto failed;
    uint64_t graphics_calls=graphics.completed;
    phase=9;uint64_t restored_client=0;
    if(!pair_restore(inspect,&restored_client))goto failed;
    for(phase=9;phase<=10;++phase){
        deadline=now()+UINT64_C(60000000000);
        for(;;){
            if(pcopy(restored_client,GPU_PAIR_STATE_ADDRESS,(uintptr_t)&shared,sizeof(shared),0,0).status || shared.magic!=GPU_PAIR_MAGIC || shared.error || now()>=deadline)goto failed;
            if(shared.phase==phase)break;
            if(!wait_tick())goto failed;
        }
        phase_log(phase,&shared);hold=now()+UINT64_C(1000000000);while(now()<hold)if(!wait_tick())goto failed;
        uint64_t ack=phase+1;if(pcopy(restored_client,GPU_PAIR_STATE_ADDRESS+16,(uintptr_t)&ack,8,1,0).status)goto failed;
    }
    deadline=now()+UINT64_C(30000000000);
    kernel_surface_call reap=kernel_surface_find(state.hub,"process.reap");
    if(!reap)goto failed;
    for(;;){struct process_info info;
        if(CALL(pinfo,restored_client,&info,0).status || info.state==PROCESS_FAULTED || info.exit_code)goto failed;
        if(info.state==PROCESS_EXITED && !CALL(reap,restored_client,0,0).status)break;
        if(now()>=deadline || !wait_tick())goto failed;
    }
    if(!CALL(inspect,GPU_PAIR_NAME,&client,0).status || CALL(inspect,"/@display",&display,0).status || CALL(inspect,"/@graphics",&graphics,0).status)goto failed;
#if GPU_PAIR_PROFILE
    if(!gp_dump())goto failed;
#endif
    log_text("GPU_PAIR RESTORE PASS client=");log_number(restored_client);log_text(" display=");log_number(display.process);log_text(" graphics=");log_number(graphics.process);log_text("\n");
    log_text("GPU_PAIR PASS accelerated=");log_number(GPU_PAIR_RENDERING);log_text(" captures=");log_number(snapshot.captures);log_text(" graphics_calls=");log_number(graphics_calls);log_text("\n");return module_result(1,1);
failed:
    log_text("GPU_PAIR FAIL phase=");log_number(phase);log_text(" client_line=");log_number(shared.error);log_text(" capture_error=");log_number(snapshot.error);log_text("\n");return module_result(0,0);
}
