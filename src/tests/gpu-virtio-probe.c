/* Replaces only the disk module in a disposable image. No candidate takeover. */
#define CB_GPU_DISCOVER_TEST gpu_virtio_test
#include "../kmodules/shared/gpu-virtio.c"
#include "../handlers/display-opengl/render.h"
#include "../interfaces/render-backend.h"
static void test_text(const char *s){size_t n=0;while(s[n])n++;kernel_surface_call log=kernel_surface_find(state.hub,"hardware.log");if(log)CALL(log,s,n,0);}
static void test_number(uint64_t n){char text[21];unsigned at=20;text[at]=0;do{text[--at]='0'+n%10;n/=10;}while(n);test_text(text+at);}
struct render_test_adapter {kernel_surface_call invoke;uint64_t process;};
static cb_status_t render_test_io(void *context,const struct cb_io_gpu_request *q,const void *data,size_t bytes,void *out,size_t capacity){
    struct render_test_adapter *adapter=context;unsigned char *input=allocate(sizeof(*q)+bytes);if(!input)return CB_RESOURCE_FAILURE;
    copy(input,q,sizeof(*q));if(bytes)copy(input+sizeof(*q),data,bytes);
    struct io_invocation r={.process=adapter->process,.operation=CB_IO_GPU,.input=input,.bytes=sizeof(*q)+bytes,.output=out,.capacity=capacity};
    struct kernel_surface_result got=CALL(adapter->invoke,&r,0,0);drop(input);
    return got.status?CB_RESOURCE_FAILURE:r.status?r.status:r.actual!=capacity?CB_BAD_REQUEST:CB_OK;
}
static void *render_test_allocate(void *unused,size_t bytes){(void)unused;return allocate(bytes);}
static void render_test_release(void *unused,void *p){(void)unused;drop(p);}
static int render_upload(struct cb_gl_display *d,uint64_t owner,uint64_t id,const void *pixels,size_t bytes){
    unsigned char *packet=allocate(sizeof(struct cb_render_write)+CB_GL_TRANSFER_BYTES);if(!packet)return 0;int ok=1;
    for(size_t at=0;at<bytes;){size_t n=bytes-at;if(n>CB_GL_TRANSFER_BYTES)n=CB_GL_TRANSFER_BYTES;struct cb_render_write q={id,at,n,(!at?CB_RENDER_WRITE_BEGIN:0)|(at+n==bytes?CB_RENDER_WRITE_COMMIT:0)};copy(packet,&q,sizeof(q));copy(packet+sizeof(q),(const unsigned char *)pixels+at,n);if(cb_gl_surface_write(d,owner,packet,sizeof(q)+n)){ok=0;break;}at+=n;}drop(packet);return ok;
}
static int test_render(void){
    struct render_test_adapter adapter={kernel_surface_find(state.hub,"io.invoke"),UINT64_C(0x4452415754455354)};
    kernel_surface_call bind=kernel_surface_find(state.hub,"io.bind"),exit=kernel_surface_find(state.hub,"io.exit");
    struct cb_gl_display display={0};uint32_t *pixels=NULL,*expected=NULL;int ok=0;
    if(!adapter.invoke || !bind || !exit || CALL(bind,"display",adapter.process,0).status)return 0;
    if(cb_gl_display_open(&display,render_test_io,&adapter,state.table.devices[0].hardware_index,64,48,0))goto done;
    pixels=allocate(64*48*4);expected=allocate(64*48*4);if(!pixels || !expected)goto done;
    struct cb_rectangle fill={3,5,19,13,0x4488aa,0};
    if(cb_gl_display_fill(&display,&fill))goto done;
    for(unsigned y=5;y<18;++y)for(unsigned x=3;x<22;++x)expected[y*64+x]=fill.color;
    uint32_t source[11*9];for(unsigned i=0;i<11*9;++i)source[i]=(i*273)&0xffffff;
    struct cb_rectangle blit={7,8,11,9,0,0};if(cb_gl_display_blit(&display,&blit,source,sizeof(source)))goto done;
    for(unsigned y=0;y<9;++y)for(unsigned x=0;x<11;++x)expected[(8+y)*64+7+x]=source[y*11+x];
    struct cb_display_copy moved={9,10,7,8,21,20};if(cb_gl_display_copy(&display,&moved))goto done;
    copy(pixels,expected,64*48*4);
    for(unsigned y=0;y<20;++y)for(unsigned x=0;x<21;++x)expected[(10+y)*64+9+x]=pixels[(8+y)*64+7+x];
    if(cb_gl_display_present(&display) || cb_gl_display_read(&display,0,pixels,64*48*4))goto done;
    for(unsigned i=0;i<64*48;++i)if((pixels[i]&0xffffff)!=expected[i]){test_text("GPU_TEST DRAW PIXEL index=");test_number(i);test_text(" got=");test_number(pixels[i]&0xffffff);test_text(" expected=");test_number(expected[i]);test_text("\n");goto done;}
    cb_gl_display_heap(&display,render_test_allocate,render_test_release,NULL);
    uint64_t base=0,layer=0;if(cb_gl_surface_create(&display,adapter.process,64,48,&base) || cb_gl_surface_create(&display,adapter.process,2,2,&layer))goto done;
    uint32_t cursor[4]={0,0x80ff0000,0xff00ff00,0x40ffff00};
    if(!render_upload(&display,adapter.process,base,expected,64*48*4) || !render_upload(&display,adapter.process,layer,cursor,sizeof(cursor)))goto done;
    struct {struct cb_render_batch header;struct cb_render_layer layers[2];} frame={.header={.version={sizeof(struct cb_render_batch),1,0},.count=1,.flags=CB_RENDER_COMPOSE_BEGIN},.layers={{.surface=base,.width=64,.height=48},{.surface=layer,.x=4,.y=6,.width=2,.height=2,.flags=CB_RENDER_LAYER_ALPHA}}};
    if(cb_gl_surface_compose(&display,adapter.process,&frame,sizeof(frame.header)+sizeof(frame.layers[0])) || cb_gl_display_present(&display)!=CB_BUSY || cb_gl_display_read(&display,0,pixels,64*48*4))goto done;
    for(unsigned i=0;i<64*48;++i)if((pixels[i]&0xffffff)!=expected[i])goto done;
    frame.header.flags=CB_RENDER_COMPOSE_END;frame.layers[0]=frame.layers[1];
    if(cb_gl_surface_compose(&display,adapter.process,&frame,sizeof(frame.header)+sizeof(frame.layers[0])) || cb_gl_display_read(&display,0,pixels,64*48*4))goto done;
    for(unsigned y=0;y<2;++y)for(unsigned x=0;x<2;++x){uint32_t c=cursor[y*2+x],dest=expected[(6+y)*64+4+x],a=c>>24,value=0;for(unsigned shift=0;shift<24;shift+=8)value|=((((c>>shift)&255)*a+((dest>>shift)&255)*(255-a)+127)/255)<<shift;expected[(6+y)*64+4+x]=value;}
    for(unsigned i=0;i<64*48;++i)for(unsigned shift=0;shift<24;shift+=8){int got=(pixels[i]>>shift)&255,want=(expected[i]>>shift)&255;if(got-want>1 || want-got>1){test_text("GPU_TEST COMPOSE PIXEL index=");test_number(i);test_text(" got=");test_number(pixels[i]&0xffffff);test_text(" expected=");test_number(expected[i]);test_text("\n");goto done;}}
    if(cb_gl_surface_owner_exit(&display,adapter.process))goto done;
    test_text("GPU_TEST COMPOSE PASS pixels=3072\n");
    ok=cb_gl_display_close(&display)==CB_OK;
done:
    if(display.cookie)(void)cb_gl_display_close(&display);
    if(CALL(exit,adapter.process,0,0).status)ok=0;
    (void)CALL(bind,"display",0,0);drop(pixels);drop(expected);return ok;
}
/* Test-only kernel principal, bound before real provider startup. This checks
 * mediated IO routing and the device gateway, not a userland syscall fixture. */
static int test_bridge(void){
    const uint64_t pid=UINT64_C(0x47505554455354);
    kernel_surface_call bind=kernel_surface_find(state.hub,"io.bind"),invoke=kernel_surface_find(state.hub,"io.invoke"),exit=kernel_surface_find(state.hub,"io.exit");
    if(!bind || !invoke || !exit)return 0;
    unsigned char input[sizeof(struct cb_io_gpu_request)+1024],output[1024];
    struct cb_io_gpu_request q={.operation=CB_GPU_OPEN,.device=state.table.devices[0].hardware_index};
    struct io_invocation r={.process=pid,.operation=CB_IO_GPU,.input=input,.bytes=sizeof(q),.output=output,.capacity=sizeof(output)};
    uint64_t cookie=0;int ok=0;
#define BRIDGE() (copy(input,&q,sizeof(q)),!CALL(invoke,&r,0,0).status && r.status==CB_OK)
    if(!CALL(bind,"display",pid,0).status){
        r.process=pid+1;copy(input,&q,sizeof(q));
        if(CALL(invoke,&r,0,0).status || r.status!=CB_DENIED)goto done;
        r.process=pid;if(!BRIDGE() || r.actual!=sizeof(struct cb_io_gpu_info))goto done;
        struct cb_io_gpu_info info;copy(&info,output,sizeof(info));cookie=info.cookie;if(!cookie)goto done;
        q=(struct cb_io_gpu_request){.operation=CB_GPU_MODES,.device=cookie};
        if(!BRIDGE() || r.actual!=sizeof(struct cb_io_gpu_modes))goto done;
        struct cb_io_gpu_modes modes;copy(&modes,output,sizeof(modes));if(modes.count!=16 || !modes.modes[0].enabled || !modes.modes[0].width || !modes.modes[0].height)goto done;
        q=(struct cb_io_gpu_request){.operation=CB_GPU_CREATE,.device=cookie,.width=16,.height=16};
        if(!BRIDGE() || r.actual!=sizeof(struct cb_io_gpu_resource))goto done;
        struct cb_io_gpu_resource image;copy(&image,output,sizeof(image));if(image.bytes!=1024)goto done;
        q=(struct cb_io_gpu_request){.operation=CB_GPU_WRITE,.device=cookie,.resource=image.id,.bytes=1024};
        for(unsigned i=0;i<256;++i){uint32_t color=0xff4488aa;copy(input+sizeof(q)+i*4,&color,4);}
        r.bytes=sizeof(q)+1024;if(!BRIDGE())goto done;r.bytes=sizeof(q);
        q=(struct cb_io_gpu_request){.operation=CB_GPU_PRESENT,.device=cookie,.resource=image.id};if(!BRIDGE())goto done;
        q=(struct cb_io_gpu_request){.operation=CB_GPU_READ,.device=cookie,.resource=image.id,.bytes=1024};
        if(!BRIDGE() || r.actual!=1024)goto done;
        for(unsigned i=0;i<256;++i){uint32_t color;copy(&color,output+i*4,4);if(color!=0xff4488aa)goto done;}
        /* Exit must sweep still-live resources; a second cleanup is harmless. */
        if(CALL(exit,pid,0,0).status || CALL(exit,pid,0,0).status)goto done;
        q=(struct cb_io_gpu_request){.operation=CB_GPU_CLOSE,.device=cookie};copy(input,&q,sizeof(q));
        if(CALL(invoke,&r,0,0).status || r.status!=CB_BAD_HANDLE)goto done;
        cookie=0;ok=1;
    }
done:
    if(cookie)(void)CALL(exit,pid,0,0);
    (void)CALL(bind,"display",0,0);
#undef BRIDGE
    return ok;
}
void gpu_virtio_test(void){
    unsigned line=0;const char *phase="startup";uint64_t cookie=0;
    struct gpu_virtio_info info={0};struct gpu_virtio_resource image={0};
    kernel_surface_call open=kernel_surface_find(state.hub,"gpu_virtio.open"),close=kernel_surface_find(state.hub,"gpu_virtio.close"),create=kernel_surface_find(state.hub,"gpu_virtio.create"),destroy=kernel_surface_find(state.hub,"gpu_virtio.destroy"),present=kernel_surface_find(state.hub,"gpu_virtio.present"),caps=kernel_surface_find(state.hub,"gpu_virtio.capset"),context=kernel_surface_find(state.hub,"gpu_virtio.context"),submit=kernel_surface_find(state.hub,"gpu_virtio.submit"),readback=kernel_surface_find(state.hub,"gpu_virtio.readback");
#define CHECK(x) do{if(!(x)){line=__LINE__;goto done;}}while(0)
#define PHASE(s) do{phase=(s);test_text("GPU_TEST STAGE ");test_text(phase);test_text("\n");}while(0)
    CHECK(state.table.count&&open&&close&&create&&destroy&&present&&caps&&context&&submit&&readback);
    test_text("GPU_TEST BEGIN\n");PHASE("open");
    struct kernel_surface_result opened=CALL(open,state.table.devices[0].hardware_index,&info,0);cookie=opened.value;CHECK(!opened.status&&info.state==GPU_ONLINE);
    PHASE("2d resource");CHECK(!create(cookie,320,200,(uintptr_t)&image,0,0).status&&image.pixels&&image.bytes==320*200*4);
    uint32_t *pixels=(void *)image.pixels;
    for(unsigned y=0;y<200;y++)for(unsigned x=0;x<320;x++)pixels[y*320+x]=y<100?(x<160?0xff0000:0x00ff00):(x<160?0x0000ff:0xffffff);
    PHASE("2d present");struct gpu_virtio_present p={.cookie=cookie,.resource=image.id,.width=320,.height=200};CHECK(!CALL(present,&p,0,0).status);
    if(info.virgl){
        /* QEMU GL scanout is a texture, not necessarily QMP's RAM surface.
         * Erase the source so successful readback must recover GPU contents. */
        PHASE("2d readback");zero(pixels,image.bytes);CHECK(!CALL(readback,cookie,image.id,0).status);
        for(unsigned y=0;y<200;y++)for(unsigned x=0;x<320;x++){
            uint32_t expected=y<100?(x<160?0xff0000:0x00ff00):(x<160?0x0000ff:0xffffff),actual=pixels[y*320+x]&0xffffff;
            if(actual!=expected){test_text("GPU_TEST PIXEL x=");test_number(x);test_text(" y=");test_number(y);test_text(" expected=");test_number(expected);test_text(" actual=");test_number(actual);test_text("\n");}
            CHECK(actual==expected);
        }
        test_text("GPU_TEST READBACK PASS pixels=64000\n");
    }
    test_text("GPU_TEST FRAME 320 200\n");uint64_t time=now();CHECK(time!=UINT64_MAX&&time<UINT64_MAX-UINT64_C(2000000000)&&!CALL(state.thread,THREAD_BLOCK_UNTIL,time+UINT64_C(2000000000),0).status);
    PHASE("2d cleanup");CHECK(!CALL(destroy,cookie,image.id,0).status);image=(struct gpu_virtio_resource){0};
    if(info.virgl){
        kernel_surface_call texture=kernel_surface_find(state.hub,"gpu_virtio.texture"),attach=kernel_surface_find(state.hub,"gpu_virtio.attach"),upload=kernel_surface_find(state.hub,"gpu_virtio.upload");CHECK(texture&&attach&&upload);
        PHASE("capsets");CHECK(info.capsets);struct gpu_virtio_capset cap={0};CHECK(!caps(cookie,(uintptr_t)&cap,0,0,0,0).status&&cap.bytes);
        void *data=allocate(cap.bytes);CHECK(data);int ok=!caps(cookie,(uintptr_t)&cap,(uintptr_t)data,cap.bytes,0,0).status;drop(data);CHECK(ok);
        PHASE("context create");struct kernel_surface_result ctx=CALL(context,cookie,0,0);CHECK(!ctx.status&&ctx.value);
        PHASE("texture create/upload");CHECK(!texture(cookie,32,32,2,(uintptr_t)&image,0).status&&image.pixels);
        for(unsigned i=0;i<32*32;i++)((uint32_t *)image.pixels)[i]=0xff44aa88;
        CHECK(!attach(cookie,ctx.value,image.id,1,0,0).status&&!CALL(upload,cookie,ctx.value,image.id).status);
        PHASE("texture readback");zero((void *)image.pixels,image.bytes);CHECK(!CALL(readback,cookie,image.id,ctx.value).status);
        for(unsigned i=0;i<32*32;i++)CHECK((((uint32_t *)image.pixels)[i]&0xffffff)==0x44aa88);
        test_text("GPU_TEST TEXTURE READBACK PASS pixels=1024\n");
        PHASE("submit");uint32_t nop=0;CHECK(!submit(cookie,ctx.value,(uintptr_t)&nop,4,0,0).status);
        PHASE("context/texture cleanup");CHECK(!CALL(context,cookie,ctx.value,0).status&&!CALL(destroy,cookie,image.id,0).status);image=(struct gpu_virtio_resource){0};
        test_text("GPU_TEST VIRGL CONTEXT/SUBMIT OK\n");
    }
    CHECK(!gpu_virtio_info(state.table.devices[0].hardware_index,(uintptr_t)&info,0,0,0,0).status&&info.commands>=8&&info.interrupts);
    PHASE("close");CHECK(!CALL(close,cookie,0,0).status);cookie=0;
    PHASE("privileged IO bridge");CHECK(test_bridge());test_text("GPU_TEST BRIDGE PASS\n");
    if(info.virgl){PHASE("GPU clear/blit renderer");CHECK(test_render());test_text("GPU_TEST DRAW PASS pixels=3072\n");}
    CHECK(!gpu_virtio_info(state.table.devices[0].hardware_index,(uintptr_t)&info,0,0,0,0).status);
    test_text("GPU_TEST PASS commands=");test_number(info.commands);test_text(" interrupts=");test_number(info.interrupts);test_text(" virgl=");test_number(info.virgl);test_text("\n");return;
done:
    if(cookie)(void)CALL(close,cookie,0,0);
    test_text("GPU_TEST FAIL line=");test_number(line);test_text(" phase=");test_text(phase);test_text(" stage=");test_number(state.table.count?state.table.devices[0].stage:0);test_text(" error=");test_number(state.table.count?state.table.devices[0].error:0);test_text("\n");
#undef CHECK
#undef PHASE
}
