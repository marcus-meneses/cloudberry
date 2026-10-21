#include "../../sdk/service.h"
#include "../../interfaces/graphics.h"
#include "../../interfaces/display.h"
#include "raster.h"
#include "glyphs.h"
#include "../../interfaces/io.h"
struct surface {struct surface *next;uint64_t id,owner;struct gfx_surface raster;};
static struct cb_client client,reply;
static unsigned char input[CB_FRAME_LIMIT],pixels[CB_FRAME_LIMIT-1024];
static uint32_t bitmap_pixels[CB_FRAME_LIMIT/4];
static cb_handle_t display,memory,io;
static struct surface screen,*surfaces;
static uint64_t serial;
struct screen_context {struct screen_context *next;uint64_t owner,token;struct gfx_rect clip;int32_t matrix[6];unsigned mode;};
static struct screen_context *contexts;

static struct {unsigned active;struct cb_display_copy box;struct gfx_rect damage;} pending_copy;
static void *allocate(size_t bytes){struct cb_memory_request r={.bytes=bytes};if(cb_invoke(&client,memory,(struct cb_interface_id)CB_MEMORY_INTERFACE_ID,CB_MEMORY_ALLOCATE,&r,sizeof(r),0))return NULL;struct cb_memory_result a;cb_copy(&a,cb_result(&client),sizeof(a));return (void *)(uintptr_t)a.address;}
static void release(void *p){struct cb_memory_request r={.address=(uintptr_t)p};CB_REQUIRE(cb_invoke(&client,memory,(struct cb_interface_id)CB_MEMORY_INTERFACE_ID,CB_MEMORY_FREE,&r,sizeof(r),0)==CB_OK);}
static struct surface *find(uint64_t id,uint64_t owner){if(!id)return &screen;for(struct surface *s=surfaces;s;s=s->next)if(s->id==id && s->owner==owner)return s;return NULL;}
static void destroy(struct surface *s){struct cb_io_owner_watch watch={s->owner,s->id,0};CB_REQUIRE(cb_invoke(&client,io,(struct cb_interface_id)CB_IO_INTERFACE_ID,CB_IO_OWNER_WATCH,&watch,sizeof(watch),0)==CB_OK);struct surface **at=&surfaces;while(*at!=s)at=&(*at)->next;*at=s->next;release(s->raster.pixels);release(s);}
static cb_status_t present(void){
    struct gfx_surface *s=&screen.raster;
    if(pending_copy.active){
        cb_status_t status=cb_invoke(&client,display,(struct cb_interface_id)CB_DISPLAY_INTERFACE_ID,CB_DISPLAY_COPY,&pending_copy.box,sizeof(pending_copy.box),0);
        pending_copy.active=0;
        if(status){
            /* Never retry a possibly partial overlapping copy. Repair from the
             * authoritative backing pixels on the next presentation instead. */
            gfx_damage(s,(struct gfx_rect){pending_copy.box.x,pending_copy.box.y,pending_copy.box.width,pending_copy.box.height});
            if(status!=CB_UNAVAILABLE)return status;
        }
    }
    if(!s->dirty)return CB_OK;
    /* A clear should not send a whole framebuffer through chunked RPCs.
     * Inspect final pixels, so clipping, transforms, composition and existing
     * pending damage retain exactly the same result as the blit path. */
    uint32_t color;
    if(gfx_uniform_rgb(s,s->damage,&color)){
        struct cb_rectangle r={(uint32_t)s->damage.x,(uint32_t)s->damage.y,(uint32_t)s->damage.width,(uint32_t)s->damage.height,color,0};
        cb_status_t status=cb_invoke(&client,display,(struct cb_interface_id)CB_DISPLAY_INTERFACE_ID,CB_DISPLAY_FILL,&r,sizeof(r),0);
        if(!status)gfx_reset_damage(s);
        return status;
    }
    const uint32_t capacity=(sizeof(pixels)-sizeof(struct cb_rectangle))/4;
    int32_t end_y=s->damage.y+s->damage.height,end_x=s->damage.x+s->damage.width;
    for(int32_t y=s->damage.y;y<end_y;){
        uint32_t rows=(uint32_t)s->damage.width>capacity?1:capacity/(uint32_t)s->damage.width;if(rows>(uint32_t)(end_y-y))rows=end_y-y;
        for(int32_t x=s->damage.x;x<end_x;){uint32_t n=end_x-x;if(n>capacity)n=capacity;struct cb_rectangle r={(uint32_t)x,(uint32_t)y,n,rows,0,0};cb_copy(pixels,&r,sizeof(r));
            for(uint32_t row=0;row<rows;++row)for(uint32_t i=0;i<n;++i){uint32_t rgb=gfx_rgb(s->pixels[(size_t)(y+row)*s->width+x+i]);cb_copy(pixels+sizeof(r)+((size_t)row*n+i)*4,&rgb,4);}
            cb_status_t status=cb_invoke(&client,display,(struct cb_interface_id)CB_DISPLAY_INTERFACE_ID,CB_DISPLAY_BLIT,pixels,sizeof(r)+(size_t)n*rows*4,0);if(status)return status;x+=n;
        }
        y+=rows;
    }
    gfx_reset_damage(s);return CB_OK;
}
static int covers(struct gfx_rect outer,struct gfx_rect inner){return outer.x<=inner.x && outer.y<=inner.y && (int64_t)outer.x+outer.width>=(int64_t)inner.x+inner.width && (int64_t)outer.y+outer.height>=(int64_t)inner.y+inner.height;}
static void select_context(uint64_t owner){
    struct gfx_surface *r=&screen.raster;struct screen_context *c=contexts;while(c && c->owner!=owner)c=c->next;
    r->clip=c?c->clip:(struct gfx_rect){0,0,r->width,r->height};r->mode=c?c->mode:GFX_SOURCE;
    for(unsigned i=0;i<6;++i)r->matrix[i]=c?c->matrix[i]:(i==0 || i==3?65536:0);
}
static cb_status_t save_context(uint64_t owner){
    struct screen_context *c=contexts;while(c && c->owner!=owner)c=c->next;
    if(!c){if(serial==UINT64_MAX)return CB_RESOURCE_FAILURE;c=allocate(sizeof(*c));if(!c)return CB_RESOURCE_FAILURE;c->owner=owner;c->token=++serial;
        struct cb_io_owner_watch watch={owner,c->token,1};cb_status_t status=cb_invoke(&client,io,(struct cb_interface_id)CB_IO_INTERFACE_ID,CB_IO_OWNER_WATCH,&watch,sizeof(watch),0);if(status){release(c);return status;}c->next=contexts;contexts=c;}
    c->clip=screen.raster.clip;c->mode=screen.raster.mode;cb_copy(c->matrix,screen.raster.matrix,sizeof(c->matrix));return CB_OK;
}
static void drop_context(struct screen_context *c){struct cb_io_owner_watch watch={c->owner,c->token,0};CB_REQUIRE(cb_invoke(&client,io,(struct cb_interface_id)CB_IO_INTERFACE_ID,CB_IO_OWNER_WATCH,&watch,sizeof(watch),0)==CB_OK);struct screen_context **at=&contexts;while(*at!=c)at=&(*at)->next;*at=c->next;release(c);}
static int screen_access(unsigned op,const unsigned char *payload,size_t bytes){
    if(op==CB_GRAPHICS_INFO || op==CB_GRAPHICS_SURFACE_CREATE)return 0;
    if(op==CB_GRAPHICS_FILL || op==CB_GRAPHICS_GLYPH || op==CB_GRAPHICS_GLYPHS)return 1;
    if(bytes<8)return 0;
    uint64_t id;cb_copy(&id,payload,8);if(!id)return 1;
    if(op==CB_GRAPHICS_BLIT && bytes==sizeof(struct cb_graphics_blit)){struct cb_graphics_blit b;cb_copy(&b,payload,sizeof(b));if(!b.source)return 1;}
    return 0;
}
static cb_status_t operation(uint64_t owner,unsigned op,const unsigned char *p,size_t bytes,void *out,size_t *n){
    if(screen_access(op,p,bytes)){struct cb_io_console_access access={owner,CB_CONSOLE_GRAPHICS,0};cb_status_t status=cb_invoke(&client,io,(struct cb_interface_id)CB_IO_INTERFACE_ID,CB_IO_CONSOLE_ACCESS,&access,sizeof(access),0);if(status)return status;select_context(owner);}
    if(op==CB_GRAPHICS_INFO && !bytes){struct cb_graphics_info info={screen.raster.width,screen.raster.height,0,(uint64_t)screen.raster.width*screen.raster.height*4};for(struct surface *it=surfaces;it;it=it->next){++info.surfaces;info.pixel_bytes+=(uint64_t)it->raster.width*it->raster.height*4;}cb_copy(out,&info,sizeof(info));*n=sizeof(info);return CB_OK;}
    if(op==CB_GRAPHICS_FILL && bytes==sizeof(struct cb_rectangle)){struct cb_rectangle r;cb_copy(&r,p,sizeof(r));if(r.reserved || r.color>0xffffff || r.x>32767 || r.y>32767 || r.width>32767 || r.height>32767)return CB_BAD_REQUEST;struct gfx_surface *s=&screen.raster;struct gfx_rect rect={r.x,r.y,r.width,r.height};
        if(gfx_fill_rect(s,rect,r.color|0xff000000)){
            if(pending_copy.active && covers(rect,pending_copy.damage) && covers(s->clip,pending_copy.damage)){gfx_damage(s,pending_copy.damage);pending_copy.active=0;}
            if(!pending_copy.active && s->dirty && covers(rect,s->damage) && covers(s->clip,s->damage)){
                struct cb_rectangle box={s->damage.x,s->damage.y,s->damage.width,s->damage.height,r.color,0};cb_status_t status=cb_invoke(&client,display,(struct cb_interface_id)CB_DISPLAY_INTERFACE_ID,CB_DISPLAY_FILL,&box,sizeof(box),0);if(!status)gfx_reset_damage(s);return status;
            }
        }else gfx_rect(s,rect,r.color|0xff000000,1);
        return present();}
    if(op==CB_GRAPHICS_GLYPH || op==CB_GRAPHICS_GLYPHS){if(op==CB_GRAPHICS_GLYPH && bytes!=sizeof(struct cb_glyph))return CB_BAD_REQUEST;cb_status_t status=gfx_glyphs(&screen.raster,p,bytes);return status?status:present();}
    if(op==CB_GRAPHICS_SURFACE_CREATE && bytes==sizeof(struct cb_graphics_create)){struct cb_graphics_create r;cb_copy(&r,p,sizeof(r));if(!r.width || !r.height || r.width>32767 || r.height>32767 || (uint64_t)r.width*r.height>SIZE_MAX/4 || serial==UINT64_MAX)return CB_BAD_REQUEST;struct surface *s=allocate(sizeof(*s));if(!s)return CB_RESOURCE_FAILURE;uint32_t *buffer=allocate((size_t)r.width*r.height*4);if(!buffer){release(s);return CB_RESOURCE_FAILURE;}s->id=++serial;s->owner=owner;gfx_init(&s->raster,buffer,r.width,r.height);s->next=surfaces;surfaces=s;struct cb_io_owner_watch watch={owner,s->id,1};if(cb_invoke(&client,io,(struct cb_interface_id)CB_IO_INTERFACE_ID,CB_IO_OWNER_WATCH,&watch,sizeof(watch),0)!=CB_OK){surfaces=s->next;release(buffer);release(s);return CB_RESOURCE_FAILURE;}cb_copy(out,&s->id,8);*n=8;return CB_OK;}
    if(bytes<8)return CB_BAD_REQUEST;
    uint64_t id;cb_copy(&id,p,8);struct surface *s=find(id,owner);if(!s)return CB_DENIED;
    if(op==CB_GRAPHICS_SURFACE_RELEASE && bytes==8){if(!id)return CB_DENIED;destroy(s);return CB_OK;}
    if(op==CB_GRAPHICS_SURFACE_INFO && bytes==8){struct gfx_surface snapshot=s->raster;if(s==&screen && pending_copy.active)gfx_damage(&snapshot,pending_copy.damage);struct gfx_surface *r=&snapshot;struct cb_graphics_surface_info info={id,r->generation,r->width,r->height,r->damage.x,r->damage.y,r->damage.width,r->damage.height,r->dirty,0};cb_copy(out,&info,sizeof(info));*n=sizeof(info);return CB_OK;}
    if(op==CB_GRAPHICS_PRESENT && bytes==8)return id?CB_DENIED:present();
    if(op==CB_GRAPHICS_STATE && bytes==sizeof(struct cb_graphics_state)){struct cb_graphics_state state;cb_copy(&state,p,sizeof(state));if(state.reserved || state.composition>GFX_SOURCE_OVER || state.clip_width<0 || state.clip_height<0 || !gfx_transform(&s->raster,state.matrix))return CB_BAD_REQUEST;gfx_clip(&s->raster,(struct gfx_rect){state.clip_x,state.clip_y,state.clip_width,state.clip_height});s->raster.mode=state.composition;return s==&screen?save_context(owner):CB_OK;}
    if(op>=CB_GRAPHICS_LINE && op<=CB_GRAPHICS_PATH && bytes>=sizeof(struct cb_graphics_draw)){struct cb_graphics_draw d;cb_copy(&d,p,sizeof(d));if(d.fill>1 || (op!=CB_GRAPHICS_PATH && bytes!=sizeof(d)))return CB_BAD_REQUEST;struct gfx_point points[4];for(unsigned i=0;i<4;++i){if(d.x[i]<-32768 || d.x[i]>32767 || d.y[i]<-32768 || d.y[i]>32767){if(op!=CB_GRAPHICS_ARC || i!=2)return CB_BAD_REQUEST;}points[i]=(struct gfx_point){d.x[i],d.y[i]};}
        struct gfx_rect rect={d.x[0],d.y[0],d.x[1],d.y[1]};
        if(op==CB_GRAPHICS_LINE)gfx_line(&s->raster,points[0],points[1],d.color);
        else if(op==CB_GRAPHICS_RECTANGLE)gfx_rect(&s->raster,rect,d.color,d.fill);
        else if(op==CB_GRAPHICS_ELLIPSE)gfx_arc(&s->raster,rect,0,360*65536,d.color,d.fill);
        else if(op==CB_GRAPHICS_ARC)gfx_arc(&s->raster,rect,d.x[2],d.y[2],d.color,d.fill);
        else if(op==CB_GRAPHICS_BEZIER)gfx_bezier(&s->raster,points,d.color);
        else {size_t count=(bytes-sizeof(d))/sizeof(struct cb_graphics_segment);if(!count || (bytes-sizeof(d))%sizeof(struct cb_graphics_segment))return CB_BAD_REQUEST;struct gfx_point *path=allocate((count*33+1)*sizeof(*path));if(!path)return CB_RESOURCE_FAILURE;size_t used=0,contour=0;cb_status_t status=CB_OK;
            /* Validate the entire command buffer before modifying a surface. */
            for(size_t i=0;i<count;++i){struct cb_graphics_segment seg;cb_copy(&seg,p+sizeof(d)+i*sizeof(seg),sizeof(seg));if(seg.reserved || seg.command<CB_PATH_MOVE || seg.command>CB_PATH_CLOSE || (!i && seg.command!=CB_PATH_MOVE)){status=CB_BAD_REQUEST;break;}for(unsigned j=0;j<3;++j)if(seg.x[j]<-32768 || seg.x[j]>32767 || seg.y[j]<-32768 || seg.y[j]>32767)status=CB_BAD_REQUEST;}
            if(!status)for(size_t i=0;i<count;++i){struct cb_graphics_segment seg;cb_copy(&seg,p+sizeof(d)+i*sizeof(seg),sizeof(seg));if(seg.command==CB_PATH_MOVE){if(used)path[used++]=(struct gfx_point){INT32_MIN,0};contour=used;path[used++]=(struct gfx_point){seg.x[0],seg.y[0]};}else if(seg.command==CB_PATH_LINE)path[used++]=(struct gfx_point){seg.x[0],seg.y[0]};else if(seg.command==CB_PATH_CLOSE)path[used++]=path[contour];else {struct gfx_point a=path[used-1];for(int64_t t=1;t<=32;++t){int64_t u=32-t;path[used++]=(struct gfx_point){(u*u*u*a.x+3*u*u*t*seg.x[0]+3*u*t*t*seg.x[1]+t*t*t*seg.x[2])/32768,(u*u*u*a.y+3*u*u*t*seg.y[0]+3*u*t*t*seg.y[1]+t*t*t*seg.y[2])/32768};}}}
            if(!status){if(d.fill)gfx_polygon(&s->raster,path,used,d.color,1);else for(size_t i=1;i<used;++i)if(path[i-1].x!=INT32_MIN && path[i].x!=INT32_MIN)gfx_line(&s->raster,path[i-1],path[i],d.color);}release(path);return status;}
        return CB_OK;
    }
    if(op==CB_GRAPHICS_BITMAP && bytes>=sizeof(struct cb_graphics_bitmap)){struct cb_graphics_bitmap b;cb_copy(&b,p,sizeof(b));if(!b.width || !b.height || b.width>32767 || b.height>32767 || b.stride<b.width || b.opacity>255 || (uint64_t)b.stride*b.height>(bytes-sizeof(b))/4 || sizeof(b)+(uint64_t)b.stride*b.height*4!=bytes || b.x<-32768 || b.x>32767 || b.y<-32768 || b.y>32767)return CB_BAD_REQUEST;cb_copy(bitmap_pixels,p+sizeof(b),bytes-sizeof(b));gfx_bitmap(&s->raster,(struct gfx_point){b.x,b.y},bitmap_pixels,b.width,b.height,b.stride,b.opacity);return CB_OK;}
    if(op==CB_GRAPHICS_BLIT && bytes==sizeof(struct cb_graphics_blit)){struct cb_graphics_blit b;cb_copy(&b,p,sizeof(b));struct surface *source=find(b.source,owner);if(!source || b.reserved || b.opacity>255 || b.source_x<0 || b.source_y<0 || (uint32_t)b.source_x>source->raster.width || (uint32_t)b.source_y>source->raster.height || b.width>source->raster.width-b.source_x || b.height>source->raster.height-b.source_y || b.x<-32768 || b.x>32767 || b.y<-32768 || b.y>32767)return CB_BAD_REQUEST;if(!b.width || !b.height)return CB_OK;if(source==s && s==&screen && !pending_copy.active && !s->raster.dirty){
            struct gfx_point dest;struct gfx_rect from;
            if(gfx_copy_region(&s->raster,(struct gfx_point){b.x,b.y},(struct gfx_rect){b.source_x,b.source_y,b.width,b.height},b.opacity,&dest,&from)){
                (void)gfx_self_blit(&s->raster,(struct gfx_point){b.x,b.y},(struct gfx_rect){b.source_x,b.source_y,b.width,b.height},b.opacity);
                if(s->raster.dirty){pending_copy.active=1;pending_copy.box=(struct cb_display_copy){dest.x,dest.y,from.x,from.y,from.width,from.height};pending_copy.damage=s->raster.damage;gfx_reset_damage(&s->raster);}return CB_OK;
            }
        }
        if(source==s && gfx_self_blit(&s->raster,(struct gfx_point){b.x,b.y},(struct gfx_rect){b.source_x,b.source_y,b.width,b.height},b.opacity))return CB_OK;
        uint32_t *copy=allocate((size_t)b.width*b.height*4);if(!copy)return CB_RESOURCE_FAILURE;for(unsigned y=0;y<b.height;++y)cb_copy(copy+(size_t)y*b.width,source->raster.pixels+(size_t)(b.source_y+y)*source->raster.width+b.source_x,b.width*4);gfx_bitmap(&s->raster,(struct gfx_point){b.x,b.y},copy,b.width,b.height,b.width,b.opacity);release(copy);return CB_OK;}
    return CB_BAD_REQUEST;
}
__attribute__((noreturn)) void _start(const struct cb_startup *startup){
    (void)startup;io=cb_acquire(CB_IO_NAME,(struct cb_interface_id)CB_IO_INTERFACE_ID);display=cb_acquire(CB_DISPLAY_NAME,(struct cb_interface_id)CB_DISPLAY_INTERFACE_ID);memory=cb_acquire(CB_MEMORY_NAME,(struct cb_interface_id)CB_MEMORY_INTERFACE_ID);CB_REQUIRE(cb_invoke(&client,display,(struct cb_interface_id)CB_DISPLAY_INTERFACE_ID,CB_DISPLAY_INFO,NULL,0,0)==CB_OK);struct cb_display_info info;cb_copy(&info,cb_result(&client),sizeof(info));CB_REQUIRE(info.width && info.height && info.width<=32767 && info.height<=32767);uint32_t *buffer=allocate((size_t)info.width*info.height*4);CB_REQUIRE(buffer);gfx_init(&screen.raster,buffer,info.width,info.height);gfx_damage(&screen.raster,(struct gfx_rect){0,0,info.width,info.height});
    static const char *names[]={"info","fill","glyph","surface_create","surface_release","surface_info","state","line","rectangle","ellipse","arc","bezier","path","bitmap","blit","present","glyphs"};cb_handle_t pub=cb_publish(&client,CB_GRAPHICS_NAME,(struct cb_interface_id)CB_GRAPHICS_INTERFACE_ID,names,17);
    for(;;){uint64_t bytes;CB_REQUIRE(cb_system_receive(input,sizeof(input),&bytes,0)==CB_OK && bytes>=sizeof(struct cb_delivery_header));struct cb_delivery_header d;cb_copy(&d,input,sizeof(d));if(d.call.flags&CB_CALL_SYSTEM_EVENT){if(d.call.operation==CB_IO_OWNER_EXIT && d.call.payload_bytes==8){struct surface *s=surfaces;while(s){struct surface *next=s->next;if(s->id==d.call.handle)destroy(s);s=next;}struct screen_context *c=contexts;while(c){struct screen_context *next=c->next;if(c->token==d.call.handle)drop_context(c);c=next;}continue;}if(d.call.operation==CB_HANDLE_RELEASE){while(surfaces)destroy(surfaces);while(contexts)drop_context(contexts);release(screen.raster.pixels);cb_system_release(pub);cb_system_release(display);cb_system_release(memory);cb_exit(0);}continue;}
        struct cb_admission caller;CB_REQUIRE(d.context_bytes<=bytes-sizeof(d) && d.call.payload_bytes==bytes-sizeof(d)-d.context_bytes);cb_status_t status=CB_DENIED;size_t n=0;unsigned char out[sizeof(struct cb_graphics_surface_info)];if(d.call.handle==pub && cb_admission_parse(input+sizeof(d),d.context_bytes,&caller))status=operation(caller.process,d.call.operation,input+sizeof(d)+d.context_bytes,d.call.payload_bytes,out,&n);CB_REQUIRE(cb_respond(&reply,&d,status,out,n)==CB_OK);
    }
}
