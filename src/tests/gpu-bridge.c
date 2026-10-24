/* Run the real gateway and driver against the existing deterministic device,
 * including fenced DMA transfers and IRQ wakeups. Never map user pointers. */
#define main gpu_native_test_main
#include "gpu-virtio.c"
#undef main
static unsigned char response[32768];
static cb_status_t bridge(uint64_t pid,uint64_t bound,struct cb_io_gpu_request q,const void *data,size_t bytes,size_t capacity){
    unsigned char input[sizeof(q)+64];assert(bytes<=64);memcpy(input,&q,sizeof(q));if(bytes)memcpy(input+sizeof(q),data,bytes);
    struct io_invocation r={.process=pid,.operation=CB_IO_GPU,.input=input,.bytes=sizeof(q)+bytes,.output=response,.capacity=capacity};
    memset(response,0xa5,sizeof(response));assert(!gpu_virtio_io((uintptr_t)&r,bound,0,0,0,0).status);
    assert(r.actual<=capacity);if(r.status)assert(!r.actual);return r.status;
}
static struct kernel_surface_result fail_cleanup(MODULE_ARGS){MODULE_UNUSED();return module_result(0,0);}
int main(void){
    initialize();struct cb_io_gpu_request selected={.operation=CB_GPU_SELECTED};
    assert(bridge(10,10,selected,NULL,0,sizeof(response))==CB_NOT_FOUND);
    state.selected=&model_controller;
    assert(bridge(10,10,selected,NULL,0,sizeof(response))==CB_OK && ((struct cb_io_gpu_info *)response)->hardware_index==7);
    selected.device=7;assert(bridge(10,10,selected,NULL,0,sizeof(response))==CB_BAD_REQUEST);
    state.selected=NULL;
    struct cb_io_gpu_request q={.operation=CB_GPU_NEXT,.device=UINT64_MAX};
    assert(bridge(10,10,q,NULL,0,sizeof(response))==CB_OK && !owned);
    struct cb_io_gpu_info discovered;memcpy(&discovered,response,sizeof(discovered));assert(discovered.hardware_index==7 && !discovered.cookie);
    q.device=7;assert(bridge(10,10,q,NULL,0,sizeof(response))==CB_NOT_FOUND);
    q=(struct cb_io_gpu_request){.operation=CB_GPU_OPEN,.device=7};
    assert(bridge(10,0,q,NULL,0,sizeof(response))==CB_DENIED);
    assert(bridge(11,10,q,NULL,0,sizeof(response))==CB_DENIED && !owned && !owner_pins);
    assert(bridge(10,10,q,NULL,0,64)==CB_BUFFER_TOO_SMALL && !owned);
    q.flags=1;assert(bridge(10,10,q,NULL,0,sizeof(response))==CB_BAD_REQUEST && !owned);q.flags=0;
    assert(bridge(10,10,q,NULL,0,sizeof(response))==CB_OK);
    struct cb_io_gpu_info info;memcpy(&info,response,sizeof(info));uint64_t cookie=info.cookie;
    assert(cookie && model_controller.bridge_process==10 && owner_pins==1);
    struct cb_io_gpu_request modes_request={.operation=CB_GPU_MODES,.device=cookie};
    unsigned mode_commands=model_info.commands;
    assert(bridge(10,10,modes_request,NULL,0,8)==CB_BUFFER_TOO_SMALL && model_info.commands==mode_commands);
    assert(bridge(10,10,modes_request,NULL,0,sizeof(response))==CB_OK);
    struct cb_io_gpu_modes modes;memcpy(&modes,response,sizeof(modes));assert(modes.count==16 && modes.modes[0].enabled && modes.modes[0].width==1280 && modes.modes[0].height==800);
    assert(bridge(11,11,q,NULL,0,sizeof(response))==CB_BUSY); /* replacement cannot steal */
    q.operation=CB_GPU_INFO;assert(bridge(11,11,q,NULL,0,sizeof(response))==CB_OK);
    memcpy(&info,response,sizeof(info));assert(!info.cookie);
    q=(struct cb_io_gpu_request){.operation=CB_GPU_CREATE,.device=cookie,.width=32,.height=24};
    assert(bridge(11,11,q,NULL,0,sizeof(response))==CB_BAD_HANDLE && dma_live==1);
    assert(bridge(10,10,q,NULL,0,8)==CB_BUFFER_TOO_SMALL && dma_live==1);
    assert(bridge(10,10,q,NULL,0,sizeof(response))==CB_OK);
    struct cb_io_gpu_resource image;memcpy(&image,response,sizeof(image));assert(image.bytes==32*24*4 && image.format==2 && image.id);
    uint32_t id=image.id;unsigned char payload[32];memset(payload,0x37,sizeof(payload));
    q=(struct cb_io_gpu_request){.operation=CB_GPU_WRITE,.device=cookie,.resource=id,.offset=3,.bytes=sizeof(payload)};
    assert(bridge(10,10,q,payload,31,sizeof(response))==CB_BAD_REQUEST);
    assert(bridge(10,10,q,payload,sizeof(payload),sizeof(response))==CB_OK);
    q.operation=CB_GPU_READ;assert(bridge(10,10,q,NULL,0,31)==CB_BUFFER_TOO_SMALL);
    assert(bridge(10,10,q,NULL,0,sizeof(response))==CB_OK && !memcmp(response,payload,sizeof(payload)));
    q.offset=UINT64_MAX;assert(bridge(10,10,q,NULL,0,sizeof(response))==CB_BAD_REQUEST);q.offset=0;q.bytes=UINT64_MAX;
    assert(bridge(10,10,q,NULL,0,sizeof(response))==CB_BAD_REQUEST);
    q=(struct cb_io_gpu_request){.operation=CB_GPU_PRESENT,.device=cookie,.resource=id};
    assert(bridge(10,10,q,NULL,0,sizeof(response))==CB_OK);
    q=(struct cb_io_gpu_request){.operation=CB_GPU_CONTEXT,.device=cookie};
    assert(bridge(10,10,q,NULL,0,3)==CB_BUFFER_TOO_SMALL && !model_info.contexts);
    assert(bridge(10,10,q,NULL,0,sizeof(response))==CB_OK);uint32_t ctx;memcpy(&ctx,response,4);assert(ctx);
    q=(struct cb_io_gpu_request){.operation=CB_GPU_TEXTURE,.device=cookie,.width=4,.height=4,.format=2};
    assert(bridge(10,10,q,NULL,0,sizeof(response))==CB_OK);memcpy(&image,response,sizeof(image));uint32_t texture=image.id;
    q=(struct cb_io_gpu_request){.operation=CB_GPU_ATTACH,.device=cookie,.resource=texture,.context=ctx,.flags=1};
    assert(bridge(10,10,q,NULL,0,sizeof(response))==CB_OK);
    q.operation=CB_GPU_UPLOAD;q.flags=0;assert(bridge(10,10,q,NULL,0,sizeof(response))==CB_OK);
    unsigned char patch[16];memset(patch,0x3c,sizeof(patch));
    struct cb_io_gpu_request rect={.operation=CB_GPU_WRITE,.flags=1,.device=cookie,.resource=texture,.offset=20,.width=2,.height=2,.bytes=16};
    assert(bridge(11,11,rect,patch,16,sizeof(response))==CB_BAD_HANDLE);
    assert(bridge(10,10,rect,patch,16,sizeof(response))==CB_OK);
    const unsigned char *backing=(const void *)resource(&model_controller,texture)->public.pixels;
    for(unsigned i=0;i<64;++i)assert(backing[i]==((i>=20 && i<28)||(i>=36 && i<44)?0x3c:0));
    unsigned before_rect=model_info.commands;
    rect.width=4;assert(bridge(10,10,rect,patch,16,sizeof(response))==CB_BAD_REQUEST);
    rect.width=2;rect.offset=UINT64_MAX;assert(bridge(10,10,rect,patch,16,sizeof(response))==CB_BAD_REQUEST);
    rect.offset=20;rect.operation=CB_GPU_UPLOAD;rect.context=ctx;rect.bytes=0;
    assert(bridge(10,10,rect,NULL,0,sizeof(response))==CB_OK);
    assert(last_upload.x==1 && last_upload.y==1 && last_upload.width==2 && last_upload.height==2 && last_upload.offset==20 && model_info.commands==before_rect+1);
    rect.height=4;assert(bridge(10,10,rect,NULL,0,sizeof(response))==CB_BAD_REQUEST && model_info.commands==before_rect+1);
    q.operation=CB_GPU_READBACK;assert(bridge(10,10,q,NULL,0,sizeof(response))==CB_OK);
    q=(struct cb_io_gpu_request){.operation=CB_GPU_READ,.device=cookie,.resource=texture,.bytes=64};
    assert(bridge(10,10,q,NULL,0,sizeof(response))==CB_OK);for(unsigned i=0;i<64;++i)assert(response[i]==0x5a);
    q=(struct cb_io_gpu_request){.operation=CB_GPU_CAPSET,.device=cookie};
    assert(bridge(10,10,q,NULL,0,sizeof(response))==CB_OK);struct cb_io_gpu_capset cap;memcpy(&cap,response,sizeof(cap));assert(cap.bytes==64);
    q.bytes=64;assert(bridge(10,10,q,NULL,0,sizeof(cap)+63)==CB_BUFFER_TOO_SMALL);
    assert(bridge(10,10,q,NULL,0,sizeof(response))==CB_OK && response[sizeof(cap)]==0x5a);
    q=(struct cb_io_gpu_request){.operation=CB_GPU_SUBMIT,.device=cookie,.context=ctx,.bytes=4};uint32_t nop=0;
    assert(bridge(10,10,q,&nop,4,sizeof(response))==CB_OK);
    q.context++;assert(bridge(10,10,q,&nop,4,sizeof(response))==CB_BAD_HANDLE);
    /* Cleanup failures retain bindings and DMA until a later successful reset. */
    bridge_busy=1;assert(!gpu_virtio_exit(11,0,0,0,0,0).status);
    assert(gpu_virtio_exit(10,0,0,0,0,0).status && owned);bridge_busy=0;
    state.dma_free=fail_cleanup;
    assert(gpu_virtio_exit(10,0,0,0,0,0).status && owned && dma_live && owner_pins && model_controller.bridge_process==10);
    state.dma_free=dma_free_model;
    assert(!gpu_virtio_exit(10,0,0,0,0,0).status && !owned && !dma_live && !owner_pins && !model_controller.bridge_process);
    q=(struct cb_io_gpu_request){.operation=CB_GPU_OPEN,.device=7};
    assert(bridge(12,12,q,NULL,0,sizeof(response))==CB_OK);memcpy(&info,response,sizeof(info));assert(info.cookie!=cookie);
    q=(struct cb_io_gpu_request){.operation=CB_GPU_CLOSE,.device=cookie};
    assert(bridge(12,12,q,NULL,0,sizeof(response))==CB_BAD_HANDLE);
    q.device=info.cookie;assert(bridge(12,12,q,NULL,0,sizeof(response))==CB_OK && !owned && !dma_live && !owner_pins);
    assert(bridge(12,12,q,NULL,0,sizeof(response))==CB_BAD_HANDLE);
    puts("PASS GPU bridge: provider authority, pointer-free copies, bounds, ownership, capsets/context/submit, readback, stale cookies and retained/retried exit cleanup");
}
