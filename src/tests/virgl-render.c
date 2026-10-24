/* Real installed virglrenderer + Mesa surfaceless software GL. This tests actual
 * command execution/pixels without KVM, a render node or a visible host window. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "virglrenderer-api.h"
#include "../handlers/display-opengl/render.h"
#include "../interfaces/render-backend.h"
#include "../handlers/graphics-opengl/virgl.h"
struct resource {struct resource *next;uint32_t id,w,h;void *pixels;struct iovec iov;};
static struct resource *resources;
static uint32_t serial,ctx;static uint64_t active_cookie,cookie_serial;
static unsigned calls,submits,presents,fail_next,fail_close,busy_after_submit,uploads;
static uint64_t write_bytes;
static unsigned heap_blocks;
static void *test_heap_allocate(void *c,size_t n){(void)c;void *p=calloc(1,n);if(p)++heap_blocks;return p;}
static void test_heap_release(void *c,void *p){(void)c;assert(p && heap_blocks);--heap_blocks;free(p);}
static struct resource *find(uint32_t id){for(struct resource *r=resources;r;r=r->next)if(r->id==id)return r;return NULL;}
static void fence(void *cookie,uint32_t id){(void)cookie;(void)id;}
static cb_status_t adapter(void *context,const struct cb_io_gpu_request *q,const void *payload,size_t bytes,void *out,size_t capacity){
    (void)context;++calls;if(q->operation==CB_GPU_SUBMIT && busy_after_submit && --busy_after_submit==0)return CB_BUSY;
    if(fail_next){fail_next=0;return CB_TIMEOUT;}
    if(q->operation==CB_GPU_OPEN){assert(!active_cookie && capacity==sizeof(struct cb_io_gpu_info));active_cookie=++cookie_serial;*(struct cb_io_gpu_info *)out=(struct cb_io_gpu_info){.cookie=active_cookie,.virgl=1,.scanouts=1};return CB_OK;}
    assert(q->device==active_cookie && active_cookie);
    struct resource *r=find(q->resource);
    switch(q->operation){
    case CB_GPU_CLOSE:
        if(fail_close)return CB_RESOURCE_FAILURE;
        if(ctx)virgl_renderer_context_destroy(ctx);
        ctx=0;
        while(resources){r=resources;resources=r->next;virgl_renderer_resource_unref(r->id);free(r->pixels);free(r);}active_cookie=0;return CB_OK;
    case CB_GPU_MODES:assert(capacity==sizeof(struct cb_io_gpu_modes));*(struct cb_io_gpu_modes *)out=(struct cb_io_gpu_modes){.count=1,.modes={{.width=64,.height=48,.enabled=1}}};return CB_OK;
    case CB_GPU_CONTEXT:
        assert(!q->context && !ctx && capacity==4);ctx=1;assert(!virgl_renderer_context_create(ctx,10,"cloudberry"));*(uint32_t *)out=ctx;return CB_OK;
    case CB_GPU_TEXTURE:{
        assert(capacity==sizeof(struct cb_io_gpu_resource));r=calloc(1,sizeof(*r));assert(r);r->id=++serial;r->w=q->width;r->h=q->height;r->next=resources;resources=r;
        if(!q->flags){r->pixels=calloc((size_t)r->w*r->h,4);assert(r->pixels);r->iov=(struct iovec){r->pixels,(size_t)r->w*r->h*4};}
        struct virgl_renderer_resource_create_args args={.handle=r->id,.target=2,.format=q->format,.bind=10,.width=r->w,.height=r->h,.depth=1,.array_size=1};
        assert(!virgl_renderer_resource_create(&args,r->pixels?&r->iov:NULL,r->pixels?1:0));
        *(struct cb_io_gpu_resource *)out=(struct cb_io_gpu_resource){r->id,r->w,r->h,q->format,r->iov.iov_len};return CB_OK;
    }
    case CB_GPU_ATTACH:assert(r && q->context==ctx && q->flags==1);virgl_renderer_ctx_attach_resource(ctx,r->id);return CB_OK;
    case CB_GPU_DESTROY:{
        assert(r);struct resource **at=&resources;while(*at!=r)at=&(*at)->next;*at=r->next;virgl_renderer_resource_unref(r->id);free(r->pixels);free(r);return CB_OK;
    }
    case CB_GPU_WRITE:write_bytes+=bytes;assert(r && r->pixels && q->offset<=r->iov.iov_len && bytes==q->bytes && bytes<=r->iov.iov_len-q->offset);if(q->flags){assert(q->flags==1 && q->width && q->height && bytes==(size_t)q->width*q->height*4);for(unsigned y=0;y<q->height;++y)memcpy((char *)r->pixels+q->offset+(size_t)y*r->w*4,(const char *)payload+(size_t)y*q->width*4,(size_t)q->width*4);}else memcpy((char *)r->pixels+q->offset,payload,bytes);return CB_OK;
    case CB_GPU_READ:assert(r && r->pixels && capacity==q->bytes && q->offset+q->bytes<=r->iov.iov_len);memcpy(out,(char *)r->pixels+q->offset,capacity);return CB_OK;
    case CB_GPU_UPLOAD:case CB_GPU_READBACK:{
        assert(r && r->pixels && q->context==ctx);struct virgl_box box={0,0,0,r->w,r->h,1};if(q->flags){assert(q->operation==CB_GPU_UPLOAD && q->flags==1);box=(struct virgl_box){q->offset/4%r->w,q->offset/4/r->w,0,q->width,q->height,1};}
        if(q->operation==CB_GPU_UPLOAD)++uploads;
        int rc=q->operation==CB_GPU_UPLOAD?virgl_renderer_transfer_write_iov(r->id,ctx,0,r->w*4,r->w*r->h*4,&box,q->offset,&r->iov,1):virgl_renderer_transfer_read_iov(r->id,ctx,0,r->w*4,r->w*r->h*4,&box,0,&r->iov,1);
        assert(!rc);return CB_OK;
    }
    case CB_GPU_SUBMIT:assert(q->context==ctx && bytes==q->bytes && !(bytes&3));++submits;return virgl_renderer_submit_cmd((void *)payload,ctx,bytes/4)?CB_RESOURCE_FAILURE:CB_OK;
    case CB_GPU_PRESENT:assert(r);++presents;return CB_OK;
    default:assert(0);return CB_BAD_REQUEST;
    }
}
static void upload_surface(struct cb_gl_display *d,uint64_t owner,uint64_t id,const void *pixels,size_t bytes){
    unsigned char packet[sizeof(struct cb_render_write)+CB_GL_TRANSFER_BYTES];
    for(size_t at=0;at<bytes;){size_t n=bytes-at;if(n>CB_GL_TRANSFER_BYTES)n=CB_GL_TRANSFER_BYTES;
        struct cb_render_write q={id,at,n,(!at?CB_RENDER_WRITE_BEGIN:0)|(at+n==bytes?CB_RENDER_WRITE_COMMIT:0)};memcpy(packet,&q,sizeof(q));memcpy(packet+sizeof(q),(const char *)pixels+at,n);assert(!cb_gl_surface_write(d,owner,packet,sizeof(q)+n));at+=n;
    }
}
int main(void){
    int cookie=0;struct virgl_renderer_callbacks callbacks={.version=1,.write_fence=fence};
    if(virgl_renderer_init(&cookie,1|8,&callbacks)){fputs("FAIL: surfaceless EGL unavailable\n",stderr);return 1;}
    for(unsigned i=0;i<256;++i){float f=(float)i/255.0f;uint32_t bits;memcpy(&bits,&f,4);assert(cb_virgl_unorm8(i)==bits);}
    uint32_t words[44];struct cb_virgl encoder={words,0,5};assert(!cb_virgl_surface(&encoder,1,1,2) && !encoder.count);
    encoder.capacity=44;assert(!cb_virgl_blit(&encoder,1,2,(struct cb_virgl_box){0,0,1,1},1,2,(struct cb_virgl_box){0,0,1,1},0) && !encoder.count);
    assert(!cb_virgl_blit(&encoder,1,2,(struct cb_virgl_box){0,0,1,1},2,1,(struct cb_virgl_box){0,0,1,1},1) && !encoder.count);
    struct cb_gl_display d={0};assert(cb_gl_display_open(&d,adapter,NULL,7,0,1,0)==CB_BAD_REQUEST);
    assert(!cb_gl_display_open(&d,adapter,NULL,7,64,48,0));
    uint32_t pixels[64*48],expected[64*48];memset(expected,0,sizeof(expected));
    assert(!cb_gl_display_read(&d,0,pixels,sizeof(pixels)));for(unsigned i=0;i<64*48;++i)assert(!(pixels[i]&0xffffff));
    struct cb_rectangle fill={3,5,19,13,0x4488aa,0};assert(!cb_gl_display_fill(&d,&fill));
    for(unsigned y=5;y<18;++y)for(unsigned x=3;x<22;++x)expected[y*64+x]=fill.color;
    uint32_t source[11*9];for(unsigned i=0;i<11*9;++i)source[i]=(i*273)&0xffffff;
    struct cb_rectangle blit={7,8,11,9,0,0};assert(!cb_gl_display_blit(&d,&blit,source,sizeof(source)));
    for(unsigned y=0;y<9;++y)for(unsigned x=0;x<11;++x)expected[(8+y)*64+7+x]=source[y*11+x];
    /* Overlap vertically/horizontally in both directions. Expected uses a
     * snapshot: the two-pass GPU scratch copy must have memmove semantics. */
    struct cb_display_copy copies[]={{9,10,7,8,21,20},{1,2,9,10,21,20},{2,2,1,2,30,10},{0,2,2,2,30,10}};
    for(unsigned i=0;i<sizeof(copies)/sizeof(*copies);++i){uint32_t snapshot[64*48];memcpy(snapshot,expected,sizeof(expected));struct cb_display_copy c=copies[i];assert(!cb_gl_display_copy(&d,&c));for(unsigned y=0;y<c.height;++y)for(unsigned x=0;x<c.width;++x)expected[(c.y+y)*64+c.x+x]=snapshot[(c.source_y+y)*64+c.source_x+x];}
    assert(!cb_gl_display_read(&d,0,pixels,sizeof(pixels)));for(unsigned i=0;i<64*48;++i){if((pixels[i]&0xffffff)!=expected[i])fprintf(stderr,"pixel %u: got %06x expected %06x\n",i,pixels[i]&0xffffff,expected[i]);assert((pixels[i]&0xffffff)==expected[i]);}
    cb_gl_display_heap(&d,test_heap_allocate,test_heap_release,NULL);
    uint64_t base,layer;assert(!cb_gl_surface_create(&d,77,64,48,&base) && !cb_gl_surface_create(&d,77,4,4,&layer));
    assert(cb_gl_surface_release(&d,78,layer)==CB_BAD_HANDLE);
    uint32_t bg[64*48],fg[16];for(unsigned i=0;i<64*48;++i)bg[i]=0xff102030;for(unsigned i=0;i<16;++i)fg[i]=i?0x80ff0000:0;
    upload_surface(&d,77,base,bg,sizeof(bg));upload_surface(&d,77,layer,fg,sizeof(fg));
    struct {struct cb_render_batch header;struct cb_render_layer layers[2];} frame={.header={.version={sizeof(struct cb_render_batch),1,0},.count=2,.flags=CB_RENDER_COMPOSE_BEGIN|CB_RENDER_COMPOSE_END},.layers={{.surface=base,.width=64,.height=48},{.surface=layer,.x=3,.y=5,.width=4,.height=4,.flags=CB_RENDER_LAYER_ALPHA}}};
    struct {struct cb_render_write q;uint32_t pixel;} incomplete={{layer,0,4,CB_RENDER_WRITE_BEGIN},0};
    assert(!cb_gl_surface_write(&d,77,&incomplete,sizeof(incomplete.q)+4));
    unsigned incomplete_calls=calls;assert(cb_gl_surface_compose(&d,77,&frame,sizeof(frame))==CB_BAD_REQUEST && calls==incomplete_calls);
    upload_surface(&d,77,layer,fg,sizeof(fg));
    unsigned before_frame=calls;frame.layers[1].source_x=4;assert(cb_gl_surface_compose(&d,77,&frame,sizeof(frame))==CB_BAD_REQUEST && calls==before_frame && !d.composing);frame.layers[1].source_x=0;
    frame.header.count=1;frame.header.flags=CB_RENDER_COMPOSE_BEGIN;
    assert(!cb_gl_surface_compose(&d,77,&frame,sizeof(frame.header)+sizeof(frame.layers[0])) && d.composing);
    assert(cb_gl_display_present(&d)==CB_BUSY && cb_gl_display_fill(&d,&fill)==CB_BUSY);
    assert(!cb_gl_display_read(&d,0,pixels,sizeof(pixels)));for(unsigned i=0;i<64*48;++i)assert((pixels[i]&0xffffff)==expected[i]); /* front unchanged */
    frame.header.flags=CB_RENDER_COMPOSE_END;frame.layers[0]=frame.layers[1];
    assert(cb_gl_surface_compose(&d,78,&frame,sizeof(frame.header)+sizeof(frame.layers[0]))==CB_DENIED);
    assert(!cb_gl_surface_compose(&d,77,&frame,sizeof(frame.header)+sizeof(frame.layers[0])) && !d.composing);
    assert(!cb_gl_display_read(&d,0,pixels,sizeof(pixels)));
    for(unsigned y=0;y<48;++y)for(unsigned x=0;x<64;++x){uint32_t want=0x102030;if(x>=3 && x<7 && y>=5 && y<9 && (x!=3 || y!=5))want=0x881018;if((pixels[y*64+x]&0xffffff)!=want)fprintf(stderr,"layer pixel %u,%u: got %06x want %06x\n",x,y,pixels[y*64+x]&0xffffff,want);assert((pixels[y*64+x]&0xffffff)==want);}
    /* Rectangle transactions cannot expose uninitialized/half-written pixels,
     * cannot change shape mid-upload, and may not cross owners or bounds. */
    uint64_t uninitialized;assert(!cb_gl_surface_create(&d,77,4,4,&uninitialized));
    struct {struct cb_render_patch q;uint32_t pixels[4];} patch={{uninitialized,0,1,1,2,2,8,CB_RENDER_WRITE_BEGIN},{0xff00ff00,0xff00ff00,0xff00ff00,0xff00ff00}};
    unsigned patch_calls=calls;
    assert(cb_gl_surface_patch(&d,77,&patch,sizeof(patch.q)+8)==CB_BAD_REQUEST && calls==patch_calls);
    assert(!cb_gl_surface_release(&d,77,uninitialized));patch.q.surface=layer;patch_calls=calls;
    assert(cb_gl_surface_patch(&d,78,&patch,sizeof(patch.q)+8)==CB_BAD_HANDLE && calls==patch_calls);
    patch.q.x=4;assert(cb_gl_surface_patch(&d,77,&patch,sizeof(patch.q)+8)==CB_BAD_REQUEST && calls==patch_calls);patch.q.x=1;
    patch.q.offset=4;assert(cb_gl_surface_patch(&d,77,&patch,sizeof(patch.q)+8)==CB_BAD_REQUEST && calls==patch_calls);patch.q.offset=0;
    assert(!cb_gl_surface_patch(&d,77,&patch,sizeof(patch.q)+8));
    frame.header.flags=CB_RENDER_COMPOSE_BEGIN|CB_RENDER_COMPOSE_END;frame.layers[0]=(struct cb_render_layer){.surface=base,.width=64,.height=48};frame.header.count=2;
    assert(cb_gl_surface_compose(&d,77,&frame,sizeof(frame))==CB_BAD_REQUEST);
    patch.q.offset=8;patch.q.flags=CB_RENDER_WRITE_COMMIT;patch.q.x=0;patch_calls=calls;
    assert(cb_gl_surface_patch(&d,77,&patch,sizeof(patch.q)+8)==CB_BAD_REQUEST && calls==patch_calls);patch.q.x=1;
    assert(!cb_gl_surface_patch(&d,77,&patch,sizeof(patch.q)+8));
    assert(!cb_gl_surface_compose(&d,77,&frame,sizeof(frame)));
    assert(!cb_gl_display_read(&d,0,pixels,sizeof(pixels)));assert((pixels[6*64+4]&0xffffff)==0x00ff00 && (pixels[5*64+3]&0xffffff)==0x102030);
    frame.header.flags=CB_RENDER_COMPOSE_DAMAGE|CB_RENDER_COMPOSE_END;patch_calls=calls;
    assert(cb_gl_surface_compose(&d,77,&frame,sizeof(frame))==CB_BAD_REQUEST && calls==patch_calls);
    assert(!cb_gl_surface_owner_exit(&d,77) && !heap_blocks && !d.textures);
    assert(cb_gl_surface_release(&d,77,layer)==CB_BAD_HANDLE);
    presents=0;d.dirty=1;
    assert(!presents && !cb_gl_display_present(&d) && presents==1 && !d.dirty);assert(!cb_gl_display_present(&d) && presents==1);
    unsigned before=calls;fill.width=UINT32_MAX;assert(cb_gl_display_fill(&d,&fill)==CB_BAD_REQUEST && calls==before);
    blit.reserved=1;assert(cb_gl_display_blit(&d,&blit,source,sizeof(source))==CB_BAD_REQUEST && calls==before);
    assert(cb_gl_display_read(&d,UINT64_MAX,pixels,4)==CB_BAD_REQUEST && calls==before);
    struct {struct cb_render_batch header;struct cb_render_command ops[2];} batch={.header={.version={sizeof(struct cb_render_batch),1,0},.count=2},.ops={{.operation=CB_RENDER_FILL,.width=8,.height=8,.color=0x123456},{.operation=CB_RENDER_COPY,.x=8,.width=8,.height=8}}};
    batch.ops[1].width=UINT32_MAX;assert(cb_gl_display_batch(&d,&batch,sizeof(batch))==CB_BAD_REQUEST && calls==before);
    batch.ops[1].width=8;unsigned previous=presents;assert(!cb_gl_display_batch(&d,&batch,sizeof(batch)) && presents==previous+1);
    assert(!cb_gl_display_read(&d,0,pixels,sizeof(pixels)));for(unsigned y=0;y<8;++y)for(unsigned x=0;x<16;++x)assert((pixels[y*64+x]&0xffffff)==0x123456);
    fill.width=19;fail_next=1;assert(cb_gl_display_fill(&d,&fill)==CB_TIMEOUT);before=calls;assert(cb_gl_display_present(&d)==CB_TIMEOUT && calls==before);
    fail_close=1;assert(cb_gl_display_close(&d)==CB_RESOURCE_FAILURE && d.cookie && resources);fail_close=0;
    assert(!cb_gl_display_close(&d) && !d.cookie && !active_cookie && !resources && !ctx);assert(!cb_gl_display_close(&d));
    assert(!cb_gl_display_open(&d,adapter,NULL,7,0,0,UINT32_MAX) && d.width==64 && d.height==48 && d.scanout==0);
    cb_gl_display_heap(&d,test_heap_allocate,test_heap_release,NULL);uint64_t fresh;assert(!cb_gl_surface_create(&d,77,1,1,&fresh) && fresh>layer);assert(cb_gl_surface_release(&d,77,layer)==CB_BAD_HANDLE);
    busy_after_submit=2;assert(cb_gl_display_batch(&d,&batch,sizeof(batch))==CB_RESOURCE_FAILURE && d.failure==CB_RESOURCE_FAILURE);
    assert(!cb_gl_display_close(&d) && !resources && !ctx);
    virgl_renderer_cleanup(&cookie);printf("PASS real VirGL rendering: %u submissions, clears/fills, bitmap upload, overlap-safe copies, 3072 checked pixels, batching and cleanup retry\n",submits);
    return 0;
}
