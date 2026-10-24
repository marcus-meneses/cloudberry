#include "render.h"
#include "../../interfaces/render-backend.h"
#include "../graphics-opengl/virgl.h"
struct cb_gl_texture {struct cb_gl_texture *next;uint64_t owner,id,bytes,written;uint32_t resource,width,height;unsigned writing,ready;uint32_t patch_x,patch_y,patch_width,patch_height;};
static cb_status_t call(struct cb_gl_display *d,struct cb_io_gpu_request q,const void *p,size_t n,void *out,size_t bytes){
    q.device=d->cookie;cb_status_t s=d->io(d->context,&q,p,n,out,bytes);
    /* A prior batch command may already have executed. Provider replies must
     * retain UNKNOWN outcome even if a later transport call returns BUSY. */
    if(s){if(!d->failure)d->failure=s==CB_TIMEOUT?CB_TIMEOUT:CB_RESOURCE_FAILURE;return d->failure;}
    return CB_OK;
}
static cb_status_t submit(struct cb_gl_display *d,struct cb_virgl *s){return call(d,(struct cb_io_gpu_request){.operation=CB_GPU_SUBMIT,.context=d->ctx,.bytes=s->count*4},s->words,s->count*4,NULL,0);}
static cb_status_t texture_format(struct cb_gl_display *d,uint32_t width,uint32_t height,uint32_t format,int host,uint32_t *id){
    struct cb_io_gpu_resource r={0};cb_status_t s=call(d,(struct cb_io_gpu_request){.operation=CB_GPU_TEXTURE,.width=width,.height=height,.format=format,.flags=host},NULL,0,&r,sizeof(r));
    if(s)return s;
    if(!r.id || r.width!=width || r.height!=height || r.format!=format || r.bytes!=(host?0:(uint64_t)width*height*4)){d->failure=CB_PROVIDER_FAILED;return d->failure;}
    *id=r.id;return call(d,(struct cb_io_gpu_request){.operation=CB_GPU_ATTACH,.resource=*id,.context=d->ctx,.flags=1},NULL,0,NULL,0);
}
static cb_status_t texture(struct cb_gl_display *d,uint32_t width,uint32_t height,int host,uint32_t *id){return texture_format(d,width,height,2,host,id);}
static struct cb_virgl_box box(uint32_t x,uint32_t y,uint32_t w,uint32_t h){return (struct cb_virgl_box){x,y,w,h};}
static cb_status_t live(struct cb_gl_display *d){return !d || !d->cookie?CB_UNAVAILABLE:d->failure;}
int cb_gl_display_rectangle(const struct cb_gl_display *d,const struct cb_rectangle *r){return d && r && !r->reserved && r->color<=0xffffff && r->x<=d->width && r->y<=d->height && r->width<=d->width-r->x && r->height<=d->height-r->y;}
int cb_gl_display_copy_valid(const struct cb_gl_display *d,const struct cb_display_copy *r){return d && r && r->x<=d->width && r->y<=d->height && r->source_x<=d->width && r->source_y<=d->height && r->width<=d->width-r->x && r->width<=d->width-r->source_x && r->height<=d->height-r->y && r->height<=d->height-r->source_y;}
cb_status_t cb_gl_display_close(struct cb_gl_display *d){
    if(!d)return CB_BAD_REQUEST;
    if(!d->cookie)return CB_OK;
    cb_status_t s=d->io(d->context,&(struct cb_io_gpu_request){.operation=CB_GPU_CLOSE,.device=d->cookie},NULL,0,NULL,0);
    if(!s){while(d->textures){struct cb_gl_texture *t=d->textures;d->textures=t->next;d->release(d->heap,t);}uint64_t serial=d->surface_serial;*d=(struct cb_gl_display){.surface_serial=serial};}
    return s;
}
cb_status_t cb_gl_display_open(struct cb_gl_display *d,cb_gl_io io,void *context,uint64_t hardware,uint32_t width,uint32_t height,uint32_t scanout){
    if(!d || !io || d->cookie || ((!width)!=(!height)) || (width && (width>INT32_MAX || height>INT32_MAX || width>UINT32_MAX/4/height)))return CB_BAD_REQUEST;
    uint64_t previous_serial=d->surface_serial;
    *d=(struct cb_gl_display){.surface_serial=previous_serial,.io=io,.context=context,.width=width,.height=height,.scanout=scanout};
    struct cb_io_gpu_info info={0};cb_status_t s=io(context,&(struct cb_io_gpu_request){.operation=CB_GPU_OPEN,.device=hardware},NULL,0,&info,sizeof(info));
    if(s)return s;
    d->cookie=info.cookie;
    if(!d->cookie)return d->failure=CB_BAD_REQUEST;
    if(!info.virgl){s=CB_UNAVAILABLE;goto fail;}
    if(!width){
        struct cb_io_gpu_modes modes={0};s=call(d,(struct cb_io_gpu_request){.operation=CB_GPU_MODES},NULL,0,&modes,sizeof(modes));if(s)goto fail;
        if(modes.reserved || modes.count>CB_GPU_SCANOUT_LIMIT){s=CB_BAD_REQUEST;goto fail;}
        if(scanout==UINT32_MAX)for(unsigned i=0;i<modes.count && i<info.scanouts;++i)if(modes.modes[i].enabled && modes.modes[i].width && modes.modes[i].height){scanout=i;break;}
        if(scanout>=modes.count || scanout>=info.scanouts || !modes.modes[scanout].enabled){s=CB_UNAVAILABLE;goto fail;}
        width=modes.modes[scanout].width;height=modes.modes[scanout].height;
        if(!width || !height || width>INT32_MAX || height>INT32_MAX || width>UINT32_MAX/4/height){s=CB_BAD_REQUEST;goto fail;}
        d->width=width;d->height=height;d->scanout=scanout;
    }
    if(scanout>=info.scanouts){s=CB_UNAVAILABLE;goto fail;}
    s=call(d,(struct cb_io_gpu_request){.operation=CB_GPU_CONTEXT},NULL,0,&d->ctx,sizeof(d->ctx));if(s)goto fail;
    if(!d->ctx){s=CB_BAD_REQUEST;goto fail;}
    /* CPU backing on the target supports diagnostic readback; no client maps
     * it. Scratch/color textures are host-only, never guest pixel arrays. */
    if((s=texture(d,width,height,0,&d->screen)) || (s=texture(d,width,height,1,&d->scratch)) || (s=texture(d,1,1,1,&d->solid)))goto fail;
    uint32_t words[64];struct cb_virgl stream={words,0,64};
    if(!cb_virgl_surface(&stream,1,d->solid,2) || !cb_virgl_framebuffer(&stream,1) || !cb_virgl_clear(&stream,0xff000000) || !cb_virgl_blit(&stream,d->screen,2,box(0,0,width,height),d->solid,2,box(0,0,1,1),0)){s=CB_BAD_REQUEST;goto fail;}
    if((s=submit(d,&stream)))goto fail;
    d->dirty=1;return CB_OK;
fail:
    d->failure=s;cb_status_t cleanup=cb_gl_display_close(d);return cleanup?cleanup:s;
}
cb_status_t cb_gl_display_fill(struct cb_gl_display *d,const struct cb_rectangle *r){
    cb_status_t s=live(d);if(s)return s;if(d->composing)return CB_BUSY;if(!cb_gl_display_rectangle(d,r))return CB_BAD_REQUEST;if(!r->width || !r->height)return CB_OK;
    uint32_t words[32];struct cb_virgl stream={words,0,32};
    if(!cb_virgl_clear(&stream,r->color|0xff000000) || !cb_virgl_blit(&stream,d->screen,2,box(r->x,r->y,r->width,r->height),d->solid,2,box(0,0,1,1),0))return CB_BAD_REQUEST;
    s=submit(d,&stream);if(!s)d->dirty=1;return s;
}
cb_status_t cb_gl_display_copy(struct cb_gl_display *d,const struct cb_display_copy *r){
    cb_status_t s=live(d);if(s)return s;if(d->composing)return CB_BUSY;if(!cb_gl_display_copy_valid(d,r))return CB_BAD_REQUEST;if(!r->width || !r->height || (r->x==r->source_x && r->y==r->source_y))return CB_OK;
    uint32_t words[44];struct cb_virgl stream={words,0,44};
    if(!cb_virgl_blit(&stream,d->scratch,2,box(0,0,r->width,r->height),d->screen,2,box(r->source_x,r->source_y,r->width,r->height),0) || !cb_virgl_blit(&stream,d->screen,2,box(r->x,r->y,r->width,r->height),d->scratch,2,box(0,0,r->width,r->height),0))return CB_BAD_REQUEST;
    s=submit(d,&stream);if(!s)d->dirty=1;return s;
}
cb_status_t cb_gl_display_blit(struct cb_gl_display *d,const struct cb_rectangle *r,const void *pixels,size_t bytes){
    cb_status_t s=live(d);if(s)return s;if(d->composing)return CB_BUSY;if(!cb_gl_display_rectangle(d,r) || bytes!=(uint64_t)r->width*r->height*4 || (bytes && !pixels))return CB_BAD_REQUEST;if(!bytes)return CB_OK;
    if(d->stage && (d->stage_width!=r->width || d->stage_height!=r->height)){
        s=call(d,(struct cb_io_gpu_request){.operation=CB_GPU_DESTROY,.resource=d->stage},NULL,0,NULL,0);if(s)return s;d->stage=0;
    }
    if(!d->stage){s=texture(d,r->width,r->height,0,&d->stage);if(s)return s;d->stage_width=r->width;d->stage_height=r->height;}
    for(size_t at=0;at<bytes;){size_t n=bytes-at;if(n>CB_GL_TRANSFER_BYTES)n=CB_GL_TRANSFER_BYTES;
        s=call(d,(struct cb_io_gpu_request){.operation=CB_GPU_WRITE,.resource=d->stage,.offset=at,.bytes=n},(const unsigned char *)pixels+at,n,NULL,0);if(s)return s;at+=n;
    }
    s=call(d,(struct cb_io_gpu_request){.operation=CB_GPU_UPLOAD,.resource=d->stage,.context=d->ctx},NULL,0,NULL,0);if(s)return s;
    uint32_t words[22];struct cb_virgl stream={words,0,22};
    if(!cb_virgl_blit(&stream,d->screen,2,box(r->x,r->y,r->width,r->height),d->stage,2,box(0,0,r->width,r->height),0))return CB_BAD_REQUEST;
    s=submit(d,&stream);if(!s)d->dirty=1;return s;
}
cb_status_t cb_gl_display_present(struct cb_gl_display *d){
    cb_status_t s=live(d);if(s)return s;if(d->composing)return CB_BUSY;if(!d->dirty)return CB_OK;
    s=call(d,(struct cb_io_gpu_request){.operation=CB_GPU_PRESENT,.resource=d->screen,.scanout=d->scanout},NULL,0,NULL,0);if(!s)d->dirty=0;return s;
}
cb_status_t cb_gl_display_batch(struct cb_gl_display *d,const void *payload,size_t bytes){
    cb_status_t s=live(d);if(s)return s;
    if(d->composing)return CB_BUSY;
    if(!payload || bytes<sizeof(struct cb_render_batch))return CB_BAD_REQUEST;
    struct cb_render_batch h;__builtin_memcpy(&h,payload,sizeof(h));
    if(h.version.bytes!=sizeof(h) || h.version.major!=1 || h.version.minor || h.flags || !h.count || (bytes-sizeof(h))%sizeof(struct cb_render_command) || h.count!=(bytes-sizeof(h))/sizeof(struct cb_render_command))return CB_BAD_REQUEST;
    const unsigned char *p=(const unsigned char *)payload+sizeof(h);
    for(unsigned i=0;i<h.count;++i){struct cb_render_command c;__builtin_memcpy(&c,p+i*sizeof(c),sizeof(c));
        if(c.reserved || c.reserved2)return CB_BAD_REQUEST;
        if(c.operation==CB_RENDER_FILL){struct cb_rectangle r={c.x,c.y,c.width,c.height,c.color,0};if(c.source_x || c.source_y || !cb_gl_display_rectangle(d,&r))return CB_BAD_REQUEST;}
        else if(c.operation==CB_RENDER_COPY){struct cb_display_copy r={c.x,c.y,c.source_x,c.source_y,c.width,c.height};if(c.color || !cb_gl_display_copy_valid(d,&r))return CB_BAD_REQUEST;}
        else return CB_BAD_REQUEST;
    }
    for(unsigned i=0;i<h.count;++i){struct cb_render_command c;__builtin_memcpy(&c,p+i*sizeof(c),sizeof(c));
        if(c.operation==CB_RENDER_FILL){struct cb_rectangle r={c.x,c.y,c.width,c.height,c.color,0};s=cb_gl_display_fill(d,&r);}
        else {struct cb_display_copy r={c.x,c.y,c.source_x,c.source_y,c.width,c.height};s=cb_gl_display_copy(d,&r);}
        if(s)return s;
    }
    return cb_gl_display_present(d);
}
cb_status_t cb_gl_display_read(struct cb_gl_display *d,uint64_t offset,void *out,size_t bytes){
    cb_status_t s=live(d);if(s)return s;uint64_t total=(uint64_t)d->width*d->height*4;
    if((bytes && !out) || offset>total || bytes>total-offset)return CB_BAD_REQUEST;
    s=call(d,(struct cb_io_gpu_request){.operation=CB_GPU_READBACK,.resource=d->screen,.context=d->ctx},NULL,0,NULL,0);if(s)return s;
    for(size_t at=0;at<bytes;){size_t n=bytes-at;if(n>CB_GL_TRANSFER_BYTES)n=CB_GL_TRANSFER_BYTES;
        s=call(d,(struct cb_io_gpu_request){.operation=CB_GPU_READ,.resource=d->screen,.offset=offset+at,.bytes=n},NULL,0,(unsigned char *)out+at,n);if(s)return s;at+=n;
    }return CB_OK;
}

#include "render-surfaces.inc"
