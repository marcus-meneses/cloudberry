/* Included by the driver so its tracked resources remain the single source of
 * ownership/backing truth. No second resource map, user mappings or raw pointer
 * publication. The gateway lock covers waits too: competing calls return BUSY,
 * never spin with interrupts masked. Native kernel consumers remain trusted. */
#include "io.h"
static unsigned bridge_busy;
static uint64_t gpu_bridge_owner(const struct controller *s){return __atomic_load_n(&s->bridge_process,__ATOMIC_ACQUIRE);}
static void gpu_bridge_set_owner(struct controller *s,uint64_t pid){__atomic_store_n(&s->bridge_process,pid,__ATOMIC_RELEASE);}

static int gpu_request_valid(const struct cb_io_gpu_request *q,size_t payload){
    struct cb_io_gpu_request rest=*q;
    rest.operation=0;rest.device=0;
    if(q->operation==CB_GPU_SELECTED && q->device)return 0;
    switch(q->operation){
    case CB_GPU_INFO:case CB_GPU_OPEN:case CB_GPU_CLOSE:case CB_GPU_NEXT:case CB_GPU_MODES:case CB_GPU_SELECTED:break;
    case CB_GPU_CREATE:rest.width=rest.height=0;break;
    case CB_GPU_TEXTURE:if(q->flags>1)return 0;rest.flags=rest.width=rest.height=rest.format=0;break;
    case CB_GPU_DESTROY:case CB_GPU_PRESENT:rest.resource=0;if(q->operation==CB_GPU_PRESENT)rest.scanout=0;break;
    case CB_GPU_WRITE:case CB_GPU_UPLOAD:
        if(q->flags>1)return 0;
        if(q->flags)rest.width=rest.height=0;
        rest.flags=rest.resource=0;
        if(q->operation==CB_GPU_WRITE)rest.bytes=0;else rest.context=0;
        if(q->operation==CB_GPU_WRITE || q->flags)rest.offset=0;
        break;
    case CB_GPU_READ:rest.resource=0;rest.offset=rest.bytes=0;break;
    case CB_GPU_CONTEXT:rest.context=0;break;
    case CB_GPU_ATTACH:if(q->flags>1)return 0;rest.flags=rest.context=rest.resource=0;break;
    case CB_GPU_READBACK:rest.context=rest.resource=0;break;
    case CB_GPU_CAPSET:rest.resource=0;rest.bytes=0;break;
    case CB_GPU_SUBMIT:rest.context=0;rest.bytes=0;break;
    default:return 0;
    }
    const unsigned char *p=(const void *)&rest;
    for(size_t i=0;i<sizeof(rest);++i)if(p[i])return 0;
    return (q->operation==CB_GPU_WRITE || q->operation==CB_GPU_SUBMIT)?q->bytes==payload:payload==0;
}
static int gpu_rect_valid(const struct resource *p,const struct cb_io_gpu_request *q){
    if(!q->width || !q->height || (q->offset&3) || q->offset>=p->public.bytes)return 0;
    uint64_t pixel=q->offset/4,x=pixel%p->public.width,y=pixel/p->public.width;
    return q->width<=p->public.width-x && q->height<=p->public.height-y;
}
static cb_status_t gpu_info_reply(struct io_invocation *r,const struct gpu_virtio_info *info,int disclose_cookie){
    if(r->capacity<sizeof(struct cb_io_gpu_info))return CB_BUFFER_TOO_SMALL;
    struct cb_io_gpu_info out={
        .hardware_index=info->hardware_index,.cookie=disclose_cookie?info->cookie:0,
        .offered=info->offered,.accepted=info->accepted,.commands=info->commands,
        .interrupts=info->interrupts,.timeouts=info->timeouts,.last_fence=info->last_fence,
        .state=info->state,.error=info->error,.scanouts=info->scanouts,.capsets=info->capsets,
        .resources=info->resources,.contexts=info->contexts,.virgl=info->virgl,.stage=info->stage
    };
    copy(r->output,&out,sizeof(out));r->actual=sizeof(out);return CB_OK;
}
static cb_status_t gpu_bridge_call(struct io_invocation *r,const struct cb_io_gpu_request *q){
    if(q->operation==CB_GPU_SELECTED)return state.selected?gpu_info_reply(r,state.selected->info,gpu_bridge_owner(state.selected)==r->process):CB_NOT_FOUND;
    if(q->operation==CB_GPU_NEXT){
        struct controller *found=NULL;
        for(uint64_t i=0;i<state.table.count;++i){struct controller *s=state.controllers+i;uint64_t id=s->info->hardware_index;
            if((q->device==UINT64_MAX || id>q->device) && (!found || id<found->info->hardware_index))found=s;
        }
        return found?gpu_info_reply(r,found->info,gpu_bridge_owner(found)==r->process):CB_NOT_FOUND;
    }
    if(q->operation==CB_GPU_INFO){
        struct controller *s=hardware(q->device);
        return s?gpu_info_reply(r,s->info,gpu_bridge_owner(s)==r->process):CB_NOT_FOUND;
    }
    if(q->operation==CB_GPU_OPEN){
        if(r->capacity<sizeof(struct cb_io_gpu_info))return CB_BUFFER_TOO_SMALL;
        struct controller *s=hardware(q->device);
        if(!s)return CB_NOT_FOUND;
        if(state.selected && state.selected!=s)return CB_DENIED;
        if(gpu_bridge_owner(s) || s->owned || s->owner_pinned)return CB_BUSY;
        gpu_bridge_set_owner(s,r->process);
        struct gpu_virtio_info info={0};
        struct kernel_surface_result got=gpu_virtio_open(q->device,(uintptr_t)&info,0,0,0,0);
        /* Even a failed open can retain uncertain DMA. Keep the process binding
         * so exit can retry quiescence; never allow a replacement to steal it. */
        if(!s->owned && !s->owner_pinned)gpu_bridge_set_owner(s,0);
        if(got.status)return CB_RESOURCE_FAILURE;
        return gpu_info_reply(r,&info,1);
    }
    struct controller *s=instance(q->device);
    if(!s || !gpu_bridge_owner(s) || gpu_bridge_owner(s)!=r->process)return CB_BAD_HANDLE;
    struct kernel_surface_result got={0};
    switch(q->operation){
    case CB_GPU_MODES:{
        if(r->capacity<sizeof(struct cb_io_gpu_modes))return CB_BUFFER_TOO_SMALL;
        struct vg_display modes={0};got=gpu_virtio_display(q->device,(uintptr_t)&modes,sizeof(modes),0,0,0);
        if(!got.status){struct cb_io_gpu_modes out={.count=CB_GPU_SCANOUT_LIMIT};
            for(unsigned i=0;i<CB_GPU_SCANOUT_LIMIT;++i)out.modes[i]=(struct cb_io_gpu_mode){modes.modes[i].rect.x,modes.modes[i].rect.y,modes.modes[i].rect.width,modes.modes[i].rect.height,modes.modes[i].enabled,modes.modes[i].flags};
            copy(r->output,&out,sizeof(out));r->actual=sizeof(out);
        }break;
    }
    case CB_GPU_CLOSE:
        got=gpu_virtio_close(q->device,0,0,0,0,0);
        if(!got.status)gpu_bridge_set_owner(s,0);
        break;
    case CB_GPU_CREATE:case CB_GPU_TEXTURE:{
        if(r->capacity<sizeof(struct cb_io_gpu_resource))return CB_BUFFER_TOO_SMALL;
        if(!q->width || !q->height || q->width>UINT32_MAX/4/q->height ||
           (q->operation==CB_GPU_TEXTURE && q->format!=1 && q->format!=2))return CB_BAD_REQUEST;
        struct gpu_virtio_resource resource_record={0};
        if(q->operation==CB_GPU_CREATE)got=gpu_virtio_create(q->device,q->width,q->height,(uintptr_t)&resource_record,0,0);
        else got=gpu_virtio_texture(q->device,q->width,q->height,q->format,(uintptr_t)&resource_record,q->flags);
        if(!got.status){
            struct cb_io_gpu_resource out={resource_record.id,resource_record.width,resource_record.height,
                q->operation==CB_GPU_CREATE?2:q->format,resource_record.bytes};
            copy(r->output,&out,sizeof(out));r->actual=sizeof(out);
        }
        break;
    }
    case CB_GPU_DESTROY:
        if(!resource(s,q->resource))return CB_BAD_HANDLE;
        got=gpu_virtio_destroy(q->device,q->resource,0,0,0,0);break;
    case CB_GPU_READ:case CB_GPU_WRITE:{
        struct resource *p=resource(s,q->resource);
        if(!p)return CB_BAD_HANDLE;
        if(s->info->state!=GPU_ONLINE)return CB_PROVIDER_FAILED;
        if(!p->public.pixels || q->offset>p->public.bytes || q->bytes>p->public.bytes-q->offset)return CB_BAD_REQUEST;
        if(q->operation==CB_GPU_WRITE && q->flags){
            if(!gpu_rect_valid(p,q) || q->bytes!=(uint64_t)q->width*q->height*4)return CB_BAD_REQUEST;
            for(uint32_t y=0;y<q->height;++y)copy((void *)(p->public.pixels+q->offset+(uint64_t)y*p->public.width*4),(const unsigned char *)r->input+sizeof(*q)+(size_t)y*q->width*4,(size_t)q->width*4);
            return CB_OK;
        }
        if(q->operation==CB_GPU_READ){
            if(q->bytes>r->capacity)return CB_BUFFER_TOO_SMALL;
            copy(r->output,(void *)(p->public.pixels+q->offset),q->bytes);r->actual=q->bytes;
        }else copy((void *)(p->public.pixels+q->offset),(const unsigned char *)r->input+sizeof(*q),q->bytes);
        return CB_OK;
    }
    case CB_GPU_PRESENT:{
        struct resource *p=resource(s,q->resource);if(!p)return CB_BAD_HANDLE;
        struct gpu_virtio_present present={.cookie=q->device,.resource=q->resource,.scanout=q->scanout,
            .width=p->public.width,.height=p->public.height};
        got=gpu_virtio_present((uintptr_t)&present,0,0,0,0,0);break;
    }
    case CB_GPU_CONTEXT:
        if(!q->context && r->capacity<sizeof(uint32_t))return CB_BUFFER_TOO_SMALL;
        if(q->context && !find_context(s,q->context))return CB_BAD_HANDLE;
        got=gpu_virtio_context(q->device,q->context,0,0,0,0);
        if(!got.status && !q->context){uint32_t id=got.value;copy(r->output,&id,sizeof(id));r->actual=sizeof(id);}
        break;
    case CB_GPU_ATTACH:case CB_GPU_UPLOAD:case CB_GPU_READBACK:
        if(!resource(s,q->resource) || ((q->context || q->operation!=CB_GPU_READBACK) && !find_context(s,q->context)))return CB_BAD_HANDLE;
        if(q->operation==CB_GPU_ATTACH)got=gpu_virtio_attach(q->device,q->context,q->resource,q->flags,0,0);
        else if(q->operation==CB_GPU_UPLOAD){
            struct resource *p=resource(s,q->resource);struct vg_rect rect={0};
            if(q->flags){if(!gpu_rect_valid(p,q))return CB_BAD_REQUEST;rect=(struct vg_rect){q->offset/4%p->public.width,q->offset/4/p->public.width,q->width,q->height};}
            got=gpu_virtio_upload(q->device,q->context,q->resource,q->flags?(uintptr_t)&rect:0,0,0);
        }
        else got=gpu_virtio_readback(q->device,q->resource,q->context,0,0,0);
        break;
    case CB_GPU_CAPSET:{
        if(r->capacity<sizeof(struct cb_io_gpu_capset) || q->bytes>r->capacity-sizeof(struct cb_io_gpu_capset))return CB_BUFFER_TOO_SMALL;
        if(q->resource>=s->info->capsets)return CB_BAD_REQUEST;
        struct gpu_virtio_capset cap={.index=q->resource};
        got=gpu_virtio_capset(q->device,(uintptr_t)&cap,q->bytes?(uintptr_t)r->output+sizeof(struct cb_io_gpu_capset):0,q->bytes,0,0);
        if(!got.status){struct cb_io_gpu_capset out={cap.index,cap.id,cap.version,cap.bytes};copy(r->output,&out,sizeof(out));r->actual=sizeof(out)+(q->bytes?cap.bytes:0);}
        break;
    }
    case CB_GPU_SUBMIT:
        if(!q->bytes || (q->bytes&3) || q->bytes>COMMAND_BYTES-sizeof(struct vg_header)-sizeof(struct vg_submit))return CB_BAD_REQUEST;
        if(!find_context(s,q->context))return CB_BAD_HANDLE;
        got=gpu_virtio_submit(q->device,q->context,(uintptr_t)r->input+sizeof(*q),q->bytes,0,0);break;
    default:return CB_BAD_REQUEST;
    }
    return got.status?CB_RESOURCE_FAILURE:CB_OK;
}
struct kernel_surface_result gpu_virtio_io(MODULE_ARGS){
    MODULE_UNUSED();struct io_invocation *r=(void *)a;if(!r)return module_result(0,0);
    r->actual=0;r->status=CB_DENIED;
    /* b is the kernel's catalog-bound display PID, never supplied by userland. */
    if(!b || r->process!=b)return module_result(1,1);
    r->status=CB_BAD_REQUEST;
    if(r->operation!=CB_IO_GPU || !r->input || r->bytes<sizeof(struct cb_io_gpu_request) || (r->capacity && !r->output))return module_result(1,1);
    struct cb_io_gpu_request q;copy(&q,r->input,sizeof(q));
    if(!gpu_request_valid(&q,r->bytes-sizeof(q)))return module_result(1,1);
    r->status=CB_BUSY;
    if(!foreground() || __atomic_exchange_n(&bridge_busy,1,__ATOMIC_ACQUIRE))return module_result(1,1);
    r->status=gpu_bridge_call(r,&q);
    __atomic_store_n(&bridge_busy,0,__ATOMIC_RELEASE);return module_result(1,1);
}
struct kernel_surface_result gpu_virtio_exit(MODULE_ARGS){
    MODULE_UNUSED();if(!a)return module_result(1,1);
    /* Non-display exits must not depend on GPU affinity or lock availability. */
    int owned_by_process=0;
    for(uint64_t i=0;i<state.table.count;++i)if(gpu_bridge_owner(state.controllers+i)==a)owned_by_process=1;
    if(!owned_by_process)return module_result(1,1);
    if(!foreground() || __atomic_exchange_n(&bridge_busy,1,__ATOMIC_ACQUIRE))return module_result(0,0);
    int ok=1;
    for(uint64_t i=0;i<state.table.count;++i){struct controller *s=state.controllers+i;
        if(gpu_bridge_owner(s)!=a)continue;
        if(gpu_virtio_close(s->info->cookie,0,0,0,0,0).status)ok=0;
        else gpu_bridge_set_owner(s,0);
    }
    __atomic_store_n(&bridge_busy,0,__ATOMIC_RELEASE);return module_result(ok,ok);
}
