#include "../../sdk/service.h"
#include "../../interfaces/io.h"
#include "../../interfaces/render-backend.h"
#include "render.h"
#include "../../interfaces/memory.h"
static struct cb_client client,reply;
static unsigned char input[CB_FRAME_LIMIT],packet[CB_FRAME_LIMIT-1024];
static cb_handle_t io,memory;
static uint64_t watched;
static struct cb_gl_display screen;
static cb_status_t gpu(void *unused,const struct cb_io_gpu_request *q,const void *p,size_t n,void *out,size_t capacity){
    (void)unused;if(n>sizeof(packet)-sizeof(*q) || (n && !p))return CB_BAD_REQUEST;
    cb_copy(packet,q,sizeof(*q));cb_copy(packet+sizeof(*q),p,n);
    cb_status_t s=cb_invoke(&client,io,(struct cb_interface_id)CB_IO_INTERFACE_ID,CB_IO_GPU,packet,sizeof(*q)+n,0);if(s)return s;
    struct cb_reply_header h;cb_copy(&h,client.result,sizeof(h));
    if(client.bytes<sizeof(h) || h.result_bytes!=capacity || h.result_bytes>client.bytes-sizeof(h))return CB_BAD_REQUEST;
    cb_copy(out,cb_result(&client),capacity);return CB_OK;
}
static void *heap_allocate(void *unused,size_t bytes){(void)unused;struct cb_memory_request q={.bytes=bytes};if(cb_invoke(&client,memory,(struct cb_interface_id)CB_MEMORY_INTERFACE_ID,CB_MEMORY_ALLOCATE,&q,sizeof(q),0))return NULL;struct cb_memory_result out;cb_copy(&out,cb_result(&client),sizeof(out));return (void *)(uintptr_t)out.address;}
static void heap_release(void *unused,void *p){(void)unused;struct cb_memory_request q={.address=(uintptr_t)p};CB_REQUIRE(cb_invoke(&client,memory,(struct cb_interface_id)CB_MEMORY_INTERFACE_ID,CB_MEMORY_FREE,&q,sizeof(q),0)==CB_OK);}
static cb_status_t owner_watch(uint64_t pid,unsigned enable){struct cb_io_owner_watch q={pid,pid,enable};return cb_invoke(&client,io,(struct cb_interface_id)CB_IO_INTERFACE_ID,CB_IO_OWNER_WATCH,&q,sizeof(q),0);}
static cb_status_t watch_owner(uint64_t pid){if(watched==pid)return CB_OK;if(watched){cb_status_t s=cb_gl_surface_owner_exit(&screen,watched);if(s)return s;s=owner_watch(watched,0);if(s)return s;watched=0;}cb_status_t s=owner_watch(pid,1);if(!s)watched=pid;return s;}
static cb_status_t authorize(uint64_t pid,unsigned layer){struct cb_io_console_access q={pid,layer,0};return cb_invoke(&client,io,(struct cb_interface_id)CB_IO_INTERFACE_ID,CB_IO_CONSOLE_ACCESS,&q,sizeof(q),0);}
__attribute__((noreturn)) void _start(const struct cb_startup *startup){
    (void)startup;memory=cb_acquire(CB_MEMORY_NAME,(struct cb_interface_id)CB_MEMORY_INTERFACE_ID);io=cb_acquire(CB_IO_NAME,(struct cb_interface_id)CB_IO_INTERFACE_ID);
    struct cb_io_gpu_info device;struct cb_io_gpu_request next={.operation=CB_GPU_SELECTED};
    CB_REQUIRE(gpu(NULL,&next,NULL,0,&device,sizeof(device))==CB_OK);
    CB_REQUIRE(cb_gl_display_open(&screen,gpu,NULL,device.hardware_index,0,0,UINT32_MAX)==CB_OK);
    cb_gl_display_heap(&screen,heap_allocate,heap_release,NULL);
    CB_REQUIRE(cb_gl_display_present(&screen)==CB_OK);
    static const char *public_names[]={"info","fill","blit","copy"},*private_names[]={"info","batch","create","release","write","compose","patch"};
    /* Public readiness implies all paired-provider dependencies exist. */
    cb_handle_t backend=cb_publish(&client,CB_RENDER_BACKEND_NAME,(struct cb_interface_id)CB_RENDER_BACKEND_INTERFACE_ID,private_names,7);
    cb_handle_t pub=cb_publish(&client,CB_DISPLAY_NAME,(struct cb_interface_id)CB_DISPLAY_INTERFACE_ID,public_names,4);
    for(;;){
        uint64_t bytes;CB_REQUIRE(cb_system_receive(input,sizeof(input),&bytes,0)==CB_OK && bytes>=sizeof(struct cb_delivery_header));
        struct cb_delivery_header d;cb_copy(&d,input,sizeof(d));
        if(d.call.flags&CB_CALL_SYSTEM_EVENT){if(d.call.operation==CB_IO_OWNER_EXIT && watched && d.call.handle==watched && d.call.payload_bytes==8){CB_REQUIRE(cb_gl_surface_owner_exit(&screen,watched)==CB_OK && owner_watch(watched,0)==CB_OK);watched=0;continue;}if(d.call.operation==CB_HANDLE_RELEASE){if(watched)CB_REQUIRE(owner_watch(watched,0)==CB_OK);CB_REQUIRE(cb_gl_display_close(&screen)==CB_OK);cb_system_release(memory);cb_system_release(backend);cb_system_release(pub);cb_system_release(io);cb_exit(0);}continue;}
        cb_status_t status=CB_BAD_REQUEST;struct cb_display_info info={screen.width,screen.height};uint64_t created=0;const void *out=NULL;size_t n=0;
        if((d.call.handle==pub || d.call.handle==backend) && d.context_bytes<=bytes-sizeof(d) && d.call.payload_bytes==bytes-sizeof(d)-d.context_bytes){
            struct cb_admission caller;const unsigned char *payload=input+sizeof(d)+d.context_bytes;size_t size=d.call.payload_bytes;
            if(!cb_admission_parse(input+sizeof(d),d.context_bytes,&caller))status=CB_DENIED;
            else if(d.call.handle==backend){
                status=authorize(caller.process,CB_CONSOLE_RENDER_BACKEND);
                if(!status){if(d.call.operation==CB_RENDER_BACKEND_INFO && !size){out=&info;n=sizeof(info);}else if(d.call.operation==CB_RENDER_BACKEND_BATCH)status=cb_gl_display_batch(&screen,payload,size);
                    else if(d.call.operation==CB_RENDER_BACKEND_CREATE && size==sizeof(struct cb_render_create)){struct cb_render_create q;cb_copy(&q,payload,sizeof(q));status=watch_owner(caller.process);if(!status)status=cb_gl_surface_create(&screen,caller.process,q.width,q.height,&created);if(!status){out=&created;n=8;}}
                    else if(d.call.operation==CB_RENDER_BACKEND_RELEASE && size==8){uint64_t id;cb_copy(&id,payload,8);status=cb_gl_surface_release(&screen,caller.process,id);}
                    else if(d.call.operation==CB_RENDER_BACKEND_PATCH)status=cb_gl_surface_patch(&screen,caller.process,payload,size);
                    else if(d.call.operation==CB_RENDER_BACKEND_WRITE)status=cb_gl_surface_write(&screen,caller.process,payload,size);
                    else if(d.call.operation==CB_RENDER_BACKEND_COMPOSE)status=cb_gl_surface_compose(&screen,caller.process,payload,size);
                    else status=CB_BAD_REQUEST;}
            }else if(d.call.operation==CB_DISPLAY_INFO && !size){status=CB_OK;out=&info;n=sizeof(info);}
            else if((status=authorize(caller.process,CB_CONSOLE_DISPLAY))==CB_OK){
                status=CB_BAD_REQUEST;
                if((d.call.operation==CB_DISPLAY_FILL || d.call.operation==CB_DISPLAY_BLIT) && size>=sizeof(struct cb_rectangle)){
                    struct cb_rectangle r;cb_copy(&r,payload,sizeof(r));
                    if(d.call.operation==CB_DISPLAY_FILL && size==sizeof(r))status=cb_gl_display_fill(&screen,&r);
                    else if(d.call.operation==CB_DISPLAY_BLIT)status=cb_gl_display_blit(&screen,&r,payload+sizeof(r),size-sizeof(r));
                }else if(d.call.operation==CB_DISPLAY_COPY && size==sizeof(struct cb_display_copy)){struct cb_display_copy r;cb_copy(&r,payload,sizeof(r));status=cb_gl_display_copy(&screen,&r);}
                if(!status)status=cb_gl_display_present(&screen);
            }
        }
        CB_REQUIRE(cb_respond(&reply,&d,status,out,n)==CB_OK);
    }
}
