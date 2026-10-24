/* Production graphics decoder -> private backend -> real VirGL/software GL. */
#include <time.h>
#include "../sdk/service.h"
#include "../handlers/display-opengl/render.h"
#include "../interfaces/render-backend.h"
#include "../handlers/graphics-opengl/virgl.h"
#define main native_renderer_reference_main
#define serial native_serial
#define find native_find
#define calls native_calls
#define submits native_submits
#define presents native_presents
#define resources native_resources
#define ctx native_ctx
#define adapter native_adapter
#include "virgl-render.c"
#undef main
#undef serial
#undef find
#undef calls
#undef submits
#undef presents
#undef resources
#undef ctx
#undef adapter
static cb_status_t test_invoke(struct cb_client *,cb_handle_t,struct cb_interface_id,unsigned,const void *,size_t,uint64_t);
#define cb_invoke test_invoke
#define _start graphics_provider_start
#include "../handlers/graphics-opengl/graphics-opengl.c"
#undef _start
#undef cb_invoke
static struct cb_gl_display device;
static unsigned graphics_blocks;
static cb_status_t test_invoke(struct cb_client *c,cb_handle_t h,struct cb_interface_id interface,unsigned op,const void *p,size_t bytes,uint64_t deadline){
    (void)interface;(void)deadline;cb_status_t status=CB_OK;uint64_t value=0;size_t actual=0;
    if(h==1){const struct cb_memory_request *r=p;assert(bytes==sizeof(*r));if(op==CB_MEMORY_ALLOCATE){void *m=calloc(1,r->bytes);assert(m);++graphics_blocks;struct cb_memory_result out={(uintptr_t)m,r->bytes};memcpy(c->result+sizeof(struct cb_reply_header),&out,sizeof(out));actual=sizeof(out);}else {assert(op==CB_MEMORY_FREE && graphics_blocks);--graphics_blocks;free((void *)(uintptr_t)r->address);}}
    else if(h==2){assert(op==CB_IO_OWNER_WATCH || op==CB_IO_CONSOLE_ACCESS);}
    else if(h==4){
        if(op==CB_RENDER_BACKEND_CREATE){const struct cb_render_create *q=p;assert(bytes==sizeof(*q));status=cb_gl_surface_create(&device,777,q->width,q->height,&value);if(!status){memcpy(c->result+sizeof(struct cb_reply_header),&value,8);actual=8;}}
        else if(op==CB_RENDER_BACKEND_RELEASE){assert(bytes==8);memcpy(&value,p,8);status=cb_gl_surface_release(&device,777,value);}
        else if(op==CB_RENDER_BACKEND_PATCH)status=cb_gl_surface_patch(&device,777,p,bytes);
        else if(op==CB_RENDER_BACKEND_WRITE)status=cb_gl_surface_write(&device,777,p,bytes);
        else if(op==CB_RENDER_BACKEND_COMPOSE)status=cb_gl_surface_compose(&device,777,p,bytes);
        else {assert(op==CB_RENDER_BACKEND_INFO);struct cb_display_info info={device.width,device.height};memcpy(c->result+sizeof(struct cb_reply_header),&info,sizeof(info));actual=sizeof(info);}
    }else {assert(!"OpenGL must not use software display presentation");}
    struct cb_reply_header header={.version={sizeof(header),1,0},.status=status,.result_bytes=actual};memcpy(c->result,&header,sizeof(header));c->bytes=sizeof(header)+actual;return status;
}
static void compare(void){
    uint32_t pixels[96*64],expected[96*64];assert(!cb_gl_display_read(&device,0,pixels,sizeof(pixels)));
    for(unsigned i=0;i<96*64;++i)expected[i]=screen.raster.pixels[i]&0xffffff;
    for(struct surface *s=scene_roots;s;s=scene_next(s)){struct gfx_rect b;int64_t x,y;if(s->container || !scene_bounds(s,&b,&x,&y))continue;
        for(int32_t dy=b.y;dy<b.y+b.height;++dy)for(int32_t dx=b.x;dx<b.x+b.width;++dx){uint32_t pixel=s->raster.pixels[(size_t)(dy-y)*s->raster.width+dx-x];expected[dy*96+dx]=(s->opaque?pixel:scene_over(pixel,expected[dy*96+dx]))&0xffffff;}
    }
    for(unsigned y=0;y<64;++y)for(unsigned x=0;x<96;++x){uint32_t want=overlay_rgb(x,y,expected[y*96+x]),got=pixels[y*96+x];for(unsigned shift=0;shift<24;shift+=8){int a=(got>>shift)&255,b=(want>>shift)&255;if(abs(a-b)>1)fprintf(stderr,"pixel %u,%u got %06x expected %06x\n",x,y,got&0xffffff,want);assert(abs(a-b)<=1);}}
}
static uint64_t create(uint32_t width,uint32_t height,unsigned container){struct cb_graphics_create_ex q={width,height,container?CB_SURFACE_CONTAINER:0,0};uint64_t id;size_t n=0;assert(!operation(77,CB_GRAPHICS_SURFACE_CREATE,(void *)&q,sizeof(q),&id,&n) && n==8);return id;}
static void place(uint64_t surface,uint64_t parent,int32_t x,int32_t y){struct cb_graphics_place q={surface,parent,x,y,CB_SURFACE_VISIBLE,0};size_t n=0;unsigned char out[128];assert(!operation(77,CB_GRAPHICS_SURFACE_PLACE,(void *)&q,sizeof(q),out,&n));}
static uint64_t measure_ns(void){struct timespec t;assert(!clock_gettime(CLOCK_MONOTONIC,&t));return (uint64_t)t.tv_sec*1000000000+t.tv_nsec;}
static void measure_redraws(void){
    uint64_t leaf=create(1024,768,0);place(leaf,0,0,0);assert(!present());compare();
    for(unsigned mode=0;mode<3;++mode){
        uint64_t bytes_before=write_bytes,elapsed=0;unsigned calls_before=native_calls,uploads_before=uploads,submits_before=native_submits;
        for(unsigned i=0;i<32;++i){size_t n=0;unsigned char out[128];uint64_t start=measure_ns();
            if(mode==0){struct {struct cb_graphics_text_batch h;struct cb_glyph g;} q={{leaf,16,CB_GRAPHICS_DEFER},{.x=8,.y=8,.codepoint='A'+i%26,.foreground=0xffffff,.background=0x102030}};assert(!operation(77,CB_GRAPHICS_TEXT_BATCH,(void *)&q,sizeof(q),out,&n));}
            else if(mode==1){struct cb_graphics_draw q={.surface=leaf,.color=0xff112200+i,.fill=1,.x={8,16},.y={8,24}};assert(!operation(77,CB_GRAPHICS_RECTANGLE,(void *)&q,sizeof(q),out,&n));}
            else place(leaf,0,0,-(int32_t)(i%16));
            assert(!present());elapsed+=measure_ns()-start;compare();
        }
        if(mode<2){assert(write_bytes>bytes_before && write_bytes-bytes_before<UINT64_C(1024)*1024);assert(native_calls-calls_before<32*16);}
        else assert(write_bytes==bytes_before && uploads==uploads_before);
        printf("GRAPHICS_MEASURE {\"workload\":\"%s\",\"iterations\":32,\"elapsed_ns\":%llu,\"write_bytes\":%llu,\"gpu_calls\":%u,\"uploads\":%u,\"submits\":%u}\n",mode==0?"typing":mode==1?"editor-edit":"cached-scroll",(unsigned long long)elapsed,(unsigned long long)(write_bytes-bytes_before),native_calls-calls_before,uploads-uploads_before,native_submits-submits_before);
    }
    /* Several packed row groups, plus a hidden edit whose damage survives
     * until the cached surface becomes visible again. */
    size_t n=0;unsigned char out[128];uint64_t prior=write_bytes;
    struct cb_graphics_draw large={.surface=leaf,.color=0xff557799,.fill=1,.x={0,1000},.y={0,32}};
    assert(!operation(77,CB_GRAPHICS_RECTANGLE,(void *)&large,sizeof(large),out,&n));assert(!present());compare();
    assert(write_bytes-prior==UINT64_C(1000)*32*4);
    struct cb_graphics_place hidden={leaf,0,0,0,0,0};assert(!operation(77,CB_GRAPHICS_SURFACE_PLACE,(void *)&hidden,sizeof(hidden),out,&n));assert(!present());compare();
    prior=write_bytes;large.color=0xff665544;large.x[1]=8;large.y[1]=8;
    assert(!operation(77,CB_GRAPHICS_RECTANGLE,(void *)&large,sizeof(large),out,&n));assert(!present());assert(write_bytes==prior);
    place(leaf,0,0,0);assert(!present());compare();assert(write_bytes-prior==8*8*4);
    destroy(find(leaf,77));assert(!present());compare();
}
int main(void){
    int cookie=0;struct virgl_renderer_callbacks callbacks={.version=1,.write_fence=fence};assert(!virgl_renderer_init(&cookie,1|8,&callbacks));
    assert(!cb_gl_display_open(&device,native_adapter,NULL,7,96,64,0));cb_gl_display_heap(&device,test_heap_allocate,test_heap_release,NULL);
    memory=1;io=2;display=3;gl_backend=4;uint32_t *base=calloc(96*64,4);assert(base);gfx_init(&screen.raster,base,96,64);
    FILE *font=fopen("fonts/UbuntuMono-R.ttf","rb");assert(font);assert(!fseek(font,0,SEEK_END));long font_bytes=ftell(font);assert(font_bytes>0 && !fseek(font,0,SEEK_SET));void *font_data=malloc(font_bytes);assert(font_data && fread(font_data,1,font_bytes,font)==(size_t)font_bytes);fclose(font);assert(font_engine_open(font_data,font_bytes));
    struct cb_rectangle fill={0,0,96,64,0x123456,0};size_t n=0;unsigned char out[128];assert(!operation(77,CB_GRAPHICS_FILL,(void *)&fill,sizeof(fill),out,&n));
    uint64_t parent=create(38,25,1),leaf=create(40,20,0);place(parent,0,10,8);place(leaf,parent,-4,4);
    struct cb_graphics_draw rectangle={.surface=leaf,.color=0xff1a2345,.fill=1,.x={0,40},.y={0,20}};
    assert(!operation(77,CB_GRAPHICS_RECTANGLE,(void *)&rectangle,sizeof(rectangle),out,&n));
    struct {struct cb_graphics_text_batch h;struct cb_glyph g[2];} text={.h={leaf,16,CB_GRAPHICS_DEFER},.g={{.x=1,.y=1,.codepoint='A',.foreground=0xffffff,.background=0x1a2345},{.x=10,.y=1,.codepoint=0xe7,.foreground=0xffaa44,.background=0x1a2345}}};
    assert(!operation(77,CB_GRAPHICS_TEXT_BATCH,(void *)&text,sizeof(text),out,&n));assert(!present());compare();
    unsigned before_uploads=uploads;uint64_t before_bytes=write_bytes;place(parent,0,15,10);assert(!present());compare();assert(uploads==before_uploads && write_bytes==before_bytes);
    struct cb_graphics_scroll scroll={parent,0,-3};assert(!operation(77,CB_GRAPHICS_SURFACE_SCROLL,(void *)&scroll,sizeof(scroll),out,&n));assert(!present());compare();assert(uploads==before_uploads && write_bytes==before_bytes);
    struct {struct cb_overlay_set q;uint32_t pixels[4];} cursor={{20,18,2,2},{0,0x80ffff00,0xff00ff00,0x40ff0000}};
    assert(!overlay_call(99,CB_OVERLAY_SET,&cursor,sizeof(cursor),out,&n));compare();before_uploads=uploads;before_bytes=write_bytes;
    struct cb_overlay_move move={25,20};assert(!overlay_call(99,CB_OVERLAY_MOVE,&move,sizeof(move),out,&n));compare();assert(uploads==before_uploads && write_bytes==before_bytes);
    assert(!overlay_call(99,CB_OVERLAY_HIDE,NULL,0,out,&n));compare();assert(uploads==before_uploads && write_bytes==before_bytes);
    unsigned before_submits=native_submits;assert(!present() && native_submits==before_submits);
    measure_redraws();
    assert(operation(88,CB_GRAPHICS_SURFACE_RELEASE,(void *)&leaf,8,out,&n)==CB_DENIED);
    while(overlays)overlay_drop(overlays);
    while(surfaces)destroy(surfaces);
    while(contexts)drop_context(contexts);
    while(gl_caches)gl_drop_cache(gl_caches->key);
    assert(!graphics_blocks && !heap_blocks && !device.textures && !scene_cache.pixels && !scene_work.pixels);
    assert(!cb_gl_display_close(&device));font_engine_close();free(font_data);free(base);virgl_renderer_cleanup(&cookie);
    puts("PASS OpenGL provider: shared primitives/TTF, clipped hierarchy, cached moves/scroll, alpha cursor restoration, no-op frames and ownership cleanup against real VirGL");return 0;
}
