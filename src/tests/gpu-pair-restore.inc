/* Test-only lifecycle owner. No production readiness/release override. */
#include "gpu-pair-lifecycle.inc"
static int pair_start_provider(kernel_surface_call inspect,uint64_t filesystem,const char *bundle,const char *name){
    log_text("GPU_PAIR RESTORE start ");log_text(bundle);log_text("\n");
    kernel_surface_call launch=kernel_surface_find(state.hub,"bootstrap.launch");
    if(!launch)return 0;
    struct kernel_surface_result made=CALL(launch,filesystem,bundle,0);if(made.status)return 0;
    uint64_t deadline=now()+UINT64_C(30000000000);struct handle_publication_info publication;
    while(CALL(inspect,name,&publication,0).status)if(now()>=deadline || !wait_tick())return 0;
    return publication.process==made.value;
}
static int pair_restore(kernel_surface_call inspect,uint64_t *client){
    /* Stop dependents first, and fully reap before rebinding any kernel role. */
    if(!pair_stop_pipeline(inspect))return 0;
    for(uint64_t i=0;i<state.table.count;++i){struct controller *s=state.controllers+i;
        if(s->owned || s->owner_pinned || gpu_bridge_owner(s) || s->resources || s->contexts || s->attachments || s->dma.id || s->route.id){log_text("GPU_PAIR RESTORE GPU retained state=");log_number(s->info->state);log_text(" error=");log_number(s->info->error);log_text("\n");return 0;}
        if(GPU_PAIR_ACCEL && s->info->state!=GPU_CLOSED){log_text("GPU_PAIR RESTORE GPU not closed\n");return 0;}
        if(s->qualified){if(gpu_virtio_release(s->info->hardware_index,0,0,0,0,0).status)return 0;log_text("GPU_PAIR PRODUCTION RELEASE verified=1\n");}
    }
    /* TCG can qualify a real native-to-VGA transition using a 2D scanout too.
     * The ordinary-app checks before and after still use public handles. */
    if(!GPU_PAIR_ACCEL && state.table.count){
        if(state.table.count!=1)return 0;
        struct gpu_virtio_info info={0};struct gpu_virtio_resource image={0};
        if(gpu_virtio_open(state.controllers[0].info->hardware_index,(uintptr_t)&info,0,0,0,0).status || gpu_virtio_create(info.cookie,256,128,(uintptr_t)&image,0,0).status)return 0;
        for(size_t i=0;i<image.bytes/4;++i)((uint32_t *)image.pixels)[i]=0xffad3980;
        struct gpu_virtio_present present={.cookie=info.cookie,.resource=image.id,.width=256,.height=128};
        if(gpu_virtio_present((uintptr_t)&present,0,0,0,0,0).status || gpu_virtio_close(info.cookie,0,0,0,0,0).status)return 0;
        struct controller *s=state.controllers;
        if(s->owned || s->owner_pinned || s->resources || s->dma.id || s->route.id || s->info->state!=GPU_CLOSED)return 0;
        log_text("GPU_PAIR RESTORE native_2d_reset=1\n");
    }
    log_text("GPU_PAIR RESTORE quiesced=1\n");
    kernel_surface_call volume_info=kernel_surface_find(state.hub,"bootstrap.info"),load=kernel_surface_find(state.hub,"applications.installed_load"),start=kernel_surface_find(state.hub,"process.start");
    struct bootstrap_info volume;
    if(!volume_info || !load || !start || CALL(volume_info,&volume,0,0).status || volume.state!=BOOTSTRAP_READY)return 0;
    const char *bundles[]={"display-vga","graphics-vga","mouse","text"};
    const char *ready[]={"/@display","/@graphics","/@mouse","/@text"};
    for(unsigned i=0;i<4;++i)if(!pair_start_provider(inspect,volume.filesystem.cookie,bundles[i],ready[i]))return 0;
    struct {struct cb_startup h;struct cb_startup_file f;char name[8];} packet={
        .h={.version={sizeof(struct cb_startup),1,0},.bytes=sizeof(struct cb_startup)+sizeof(struct cb_startup_file)+7,.files=1},
        .f={7,0},.name="restore"
    };
    struct kernel_surface_result made=CALL(load,"gpu-pair",&packet,packet.h.bytes);
    if(made.status)return 0;
    *client=made.value;
    if(CALL(start,*client,0,0).status)return 0;
    uint64_t deadline=now()+UINT64_C(30000000000);struct handle_publication_info publication;
    while(CALL(inspect,GPU_PAIR_NAME,&publication,0).status)if(now()>=deadline || !wait_tick())return 0;
    return publication.process==*client;
}
