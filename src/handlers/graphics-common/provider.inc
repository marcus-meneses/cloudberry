#include "../../sdk/service.h"
#include <string.h>
#include "../../interfaces/graphics.h"
#include "../../interfaces/graphics-overlay.h"
#include "../../interfaces/display.h"
#include "../graphics-vga/raster.h"
#include "../graphics-vga/glyphs.h"
#include "../../interfaces/io.h"
struct surface {struct surface *next,*parent,*child,*last_child,*sibling;
    uint64_t id,owner,seen_generation;struct gfx_surface raster;
    int32_t x,y;unsigned attached,visible,container,opaque,layout_dirty;
#ifdef CB_GRAPHICS_OPENGL
    struct gfx_rect upload_damage;unsigned upload_dirty;
#endif
};
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
static void scene_unlink(struct surface *);
#ifdef CB_GRAPHICS_OPENGL
static void gl_damage_surface(struct surface *,struct gfx_rect);
static void gl_drop_cache(const void *);
static void gl_shutdown(void);
static cb_status_t gl_initialize(void),gl_present(void);
#endif
static void destroy(struct surface *s){
#ifdef CB_GRAPHICS_OPENGL
    gl_drop_cache(s);
#endif
scene_unlink(s);while(s->child)scene_unlink(s->child);struct cb_io_owner_watch watch={s->owner,s->id,0};CB_REQUIRE(cb_invoke(&client,io,(struct cb_interface_id)CB_IO_INTERFACE_ID,CB_IO_OWNER_WATCH,&watch,sizeof(watch),0)==CB_OK);struct surface **at=&surfaces;while(*at!=s)at=&(*at)->next;*at=s->next;if(s->raster.pixels)release(s->raster.pixels);release(s);}
static cb_status_t present(void);
static cb_status_t present_overlay(void);
static struct gfx_rect sparse_damage[64];static unsigned sparse_count,sparse_active;
#include "../graphics-vga/overlay.inc"
#include "../graphics-vga/scene.inc"
#ifdef CB_GRAPHICS_OPENGL
#include "../graphics-opengl/present.inc"
#endif
static cb_status_t present_blit(struct gfx_rect damage){
    struct gfx_surface *s=scene_output();
    const uint32_t capacity=(sizeof(pixels)-sizeof(struct cb_rectangle))/4;
    int32_t end_y=damage.y+damage.height,end_x=damage.x+damage.width;
    for(int32_t y=damage.y;y<end_y;){uint32_t rows=(uint32_t)damage.width>capacity?1:capacity/(uint32_t)damage.width;if(rows>(uint32_t)(end_y-y))rows=end_y-y;
        for(int32_t x=damage.x;x<end_x;){uint32_t n=end_x-x;if(n>capacity)n=capacity;struct cb_rectangle r={(uint32_t)x,(uint32_t)y,n,rows,0,0};cb_copy(pixels,&r,sizeof(r));
            for(uint32_t row=0;row<rows;++row)for(uint32_t i=0;i<n;++i){uint32_t rgb=overlay_rgb(x+i,y+row,gfx_rgb(s->pixels[(size_t)(y+row)*s->width+x+i]));cb_copy(pixels+sizeof(r)+((size_t)row*n+i)*4,&rgb,4);}
            cb_status_t status=cb_invoke(&client,display,(struct cb_interface_id)CB_DISPLAY_INTERFACE_ID,CB_DISPLAY_BLIT,pixels,sizeof(r)+(size_t)n*rows*4,0);if(status)return status;x+=n;}y+=rows;}return CB_OK;
}
static cb_status_t present_region(struct gfx_rect damage){
    if(damage.width<=0 || damage.height<=0)return CB_OK;
    uint32_t color;if(!gfx_uniform_rgb(scene_output(),damage,&color))return present_blit(damage);
    struct cb_rectangle fill={(uint32_t)damage.x,(uint32_t)damage.y,(uint32_t)damage.width,(uint32_t)damage.height,color,0};cb_status_t status=cb_invoke(&client,display,(struct cb_interface_id)CB_DISPLAY_INTERFACE_ID,CB_DISPLAY_FILL,&fill,sizeof(fill),0);if(status)return status;
    for(struct overlay *l=overlays;l;l=l->next)if(l->visible && intersects(damage,layer_rect(l))){struct gfx_rect r=layer_rect(l);int32_t right=r.x+r.width,bottom=r.y+r.height;if(r.x<damage.x)r.x=damage.x;if(r.y<damage.y)r.y=damage.y;if(right>damage.x+damage.width)right=damage.x+damage.width;if(bottom>damage.y+damage.height)bottom=damage.y+damage.height;r.width=right-r.x;r.height=bottom-r.y;status=present_blit(r);if(status)return status;}
    return CB_OK;
}
static cb_status_t present_overlay(void){
#ifdef CB_GRAPHICS_OPENGL
    return gl_present();
#endif
    for(unsigned i=0;i<overlay_damage_count;++i){cb_status_t status=present_region(overlay_damage[i]);if(status)return status;}overlay_damage_count=0;return CB_OK;}
static void sparse_add(struct gfx_rect r){
    if(!sparse_active)return;
    if(r.x>= (int32_t)screen.raster.width || r.y>=(int32_t)screen.raster.height)return;
    if(r.width>(int32_t)screen.raster.width-r.x)r.width=(int32_t)screen.raster.width-r.x;
    if(r.height>(int32_t)screen.raster.height-r.y)r.height=(int32_t)screen.raster.height-r.y;
    for(unsigned i=0;i<sparse_count;++i){struct gfx_rect *p=&sparse_damage[i];if(p->y==r.y && p->height==r.height && p->x<=r.x+r.width && r.x<=p->x+p->width){int32_t right=p->x+p->width>r.x+r.width?p->x+p->width:r.x+r.width;if(r.x<p->x)p->x=r.x;p->width=right-p->x;return;}}
    if(sparse_count==64){sparse_active=0;return;}sparse_damage[sparse_count++]=r;
}
static cb_status_t present(void){
#ifdef CB_GRAPHICS_OPENGL
    return gl_present();
#endif
    scene_collect();
    if(scene_copy.active){cb_status_t status=scene_apply_copy();if(status)return status;}
    struct gfx_surface *s=&screen.raster;
    if(pending_copy.active){
        cb_status_t status=cb_invoke(&client,display,(struct cb_interface_id)CB_DISPLAY_INTERFACE_ID,CB_DISPLAY_COPY,&pending_copy.box,sizeof(pending_copy.box),0);
        pending_copy.active=0;
        if(!status){(void)scene_compose(pending_copy.damage);overlay_copy_repair(&pending_copy.box);}
        if(status){
            /* Never retry a possibly partial overlapping copy. Repair from the
             * authoritative backing pixels on the next presentation instead. */
            gfx_damage(s,(struct gfx_rect){pending_copy.box.x,pending_copy.box.y,pending_copy.box.width,pending_copy.box.height});
            scene_retry=1;if(status!=CB_UNAVAILABLE)return status;
        }
    }
    if(s->dirty){if(sparse_active && sparse_count){for(unsigned i=0;i<sparse_count;++i){struct gfx_rect actual=scene_compose(sparse_damage[i]);cb_status_t status=present_region(actual);if(status){scene_retry=1;return status;}scene_forget_presented(sparse_damage[i]);}}else {struct gfx_rect actual=scene_compose(s->damage);cb_status_t status=present_region(actual);if(status){scene_retry=1;return status;}scene_forget_presented(s->damage);}gfx_reset_damage(s);}
    sparse_count=sparse_active=0;
    for(unsigned i=0;i<scene_count;++i){struct gfx_rect actual=scene_compose(scene_damage[i]);cb_status_t status=present_region(actual);if(status){scene_retry=1;return status;}}
    scene_count=scene_retry=0;if(scene_cache.pixels)scene_valid=1;return present_overlay();
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
    if(op==CB_GRAPHICS_INFO || op==CB_GRAPHICS_FONT_INFO || op==CB_GRAPHICS_SURFACE_CREATE)return 0;
    if(op==CB_GRAPHICS_FILL || op==CB_GRAPHICS_GLYPH || op==CB_GRAPHICS_GLYPHS)return 1;
    if(bytes<8)return 0;
    uint64_t id;cb_copy(&id,payload,8);if(!id)return 1;
    if(op==CB_GRAPHICS_BLIT && bytes==sizeof(struct cb_graphics_blit)){struct cb_graphics_blit b;cb_copy(&b,payload,sizeof(b));if(!b.source)return 1;}
    return 0;
}
static cb_status_t decode_operation(uint64_t owner,unsigned op,const unsigned char *p,size_t bytes,void *out,size_t *n){
    if(op!=CB_GRAPHICS_INFO && op!=CB_GRAPHICS_FONT_INFO && op!=CB_GRAPHICS_TEXT_BATCH && op!=CB_GRAPHICS_PRESENT){sparse_active=sparse_count=0;}
    if(screen_access(op,p,bytes)){struct cb_io_console_access access={owner,CB_CONSOLE_GRAPHICS,0};cb_status_t status=cb_invoke(&client,io,(struct cb_interface_id)CB_IO_INTERFACE_ID,CB_IO_CONSOLE_ACCESS,&access,sizeof(access),0);if(status)return status;select_context(owner);if(op!=CB_GRAPHICS_SURFACE_INFO)scene_select(owner);}
    if(op==CB_GRAPHICS_SURFACE_PLACE)return scene_place(owner,p,bytes);
    if(op==CB_GRAPHICS_SURFACE_SCROLL)return scene_scroll(owner,p,bytes);
    if(op==CB_GRAPHICS_INFO && !bytes){struct cb_graphics_info info={screen.raster.width,screen.raster.height,0,(uint64_t)screen.raster.width*screen.raster.height*4};if(scene_cache.pixels)info.pixel_bytes+=(uint64_t)scene_cache.width*scene_cache.height*8;for(struct surface *it=surfaces;it;it=it->next){++info.surfaces;if(!it->container)info.pixel_bytes+=(uint64_t)it->raster.width*it->raster.height*4;}cb_copy(out,&info,sizeof(info));*n=sizeof(info);return CB_OK;}
    if(op==CB_GRAPHICS_FONT_INFO && (!bytes || bytes==sizeof(struct cb_graphics_font_request))){struct cb_graphics_font_request q={0};if(bytes)cb_copy(&q,p,sizeof(q));if(q.reserved)return CB_BAD_REQUEST;cb_status_t status=font_engine_info(q.pixel_size,out);if(!status)*n=sizeof(struct cb_graphics_font);return status;}
    if(op==CB_GRAPHICS_TEXT_BATCH && bytes>sizeof(struct cb_graphics_text_batch)){
        struct cb_graphics_text_batch b;cb_copy(&b,p,sizeof(b));if(b.flags&~CB_GRAPHICS_DEFER || (bytes-sizeof(b))%sizeof(struct cb_glyph))return CB_BAD_REQUEST;struct surface *target=find(b.surface,owner);if(!target)return CB_DENIED;if(target->container)return CB_BAD_REQUEST;struct cb_graphics_font font;cb_status_t status=font_engine_info(b.pixel_size,&font);if(status)return status;
        for(size_t at=sizeof(b);at<bytes;at+=sizeof(struct cb_glyph)){struct cb_glyph g=gfx_glyph_at(p+at);if(g.codepoint>0x10ffff || (g.codepoint>=0xd800 && g.codepoint<=0xdfff) || g.reserved || g.foreground>0xffffff || g.background>0xffffff || g.x>32767 || g.y>32767)return CB_BAD_REQUEST;}
        if(!b.surface && !screen.raster.dirty){struct gfx_surface *r=&screen.raster;sparse_active=r->matrix[0]==65536 && !r->matrix[1] && !r->matrix[2] && r->matrix[3]==65536 && !r->matrix[4] && !r->matrix[5];sparse_count=0;}
        for(size_t at=sizeof(b);at<bytes;at+=sizeof(struct cb_glyph)){struct cb_glyph g=gfx_glyph_at(p+at);uint64_t generation=target->raster.generation;status=font_engine_draw(&target->raster,&g,b.pixel_size);if(status)return status;if(!b.surface && (generation!=target->raster.generation || generation==UINT64_MAX))sparse_add((struct gfx_rect){g.x,g.y,font.cell_width,font.cell_height});}return !(b.flags&CB_GRAPHICS_DEFER) && !b.surface?present():CB_OK;
    }
    if(op==CB_GRAPHICS_FILL && bytes==sizeof(struct cb_rectangle)){struct cb_rectangle r;cb_copy(&r,p,sizeof(r));if(r.reserved || r.color>0xffffff || r.x>32767 || r.y>32767 || r.width>32767 || r.height>32767)return CB_BAD_REQUEST;struct gfx_surface *s=&screen.raster;struct gfx_rect rect={r.x,r.y,r.width,r.height};
        if(gfx_fill_rect(s,rect,r.color|0xff000000)){
            if(pending_copy.active && covers(rect,pending_copy.damage) && covers(s->clip,pending_copy.damage)){gfx_damage(s,pending_copy.damage);pending_copy.active=0;}
#ifndef CB_GRAPHICS_OPENGL
            if(!scene_cache.pixels && !pending_copy.active && !overlay_damage_count && !overlay_intersects(s->damage) && s->dirty && covers(rect,s->damage) && covers(s->clip,s->damage)){
                struct cb_rectangle box={s->damage.x,s->damage.y,s->damage.width,s->damage.height,r.color,0};cb_status_t status=cb_invoke(&client,display,(struct cb_interface_id)CB_DISPLAY_INTERFACE_ID,CB_DISPLAY_FILL,&box,sizeof(box),0);if(!status)gfx_reset_damage(s);return status;
            }
#endif
        }else gfx_rect(s,rect,r.color|0xff000000,1);
        return present();}
    if(op==CB_GRAPHICS_GLYPH || op==CB_GRAPHICS_GLYPHS){if(op==CB_GRAPHICS_GLYPH && bytes!=sizeof(struct cb_glyph))return CB_BAD_REQUEST;cb_status_t status=gfx_glyphs(&screen.raster,p,bytes);return status?status:present();}
    if(op==CB_GRAPHICS_SURFACE_CREATE && (bytes==sizeof(struct cb_graphics_create) || bytes==sizeof(struct cb_graphics_create_ex))){
        struct cb_graphics_create_ex r={0};cb_copy(&r,p,bytes);
        if(!r.width || !r.height || r.width>32767 || r.height>32767 || r.flags&~CB_SURFACE_CONTAINER || r.reserved || (uint64_t)r.width*r.height>SIZE_MAX/4 || serial==UINT64_MAX)return CB_BAD_REQUEST;
        struct surface *s=allocate(sizeof(*s));if(!s)return CB_RESOURCE_FAILURE;memset(s,0,sizeof(*s));
        uint32_t *buffer=NULL;if(!(r.flags&CB_SURFACE_CONTAINER)){buffer=allocate((size_t)r.width*r.height*4);if(!buffer){release(s);return CB_RESOURCE_FAILURE;}memset(buffer,0,(size_t)r.width*r.height*4);}
        s->container=!!(r.flags&CB_SURFACE_CONTAINER);s->id=++serial;s->owner=owner;gfx_init(&s->raster,buffer,r.width,r.height);s->next=surfaces;surfaces=s;
        struct cb_io_owner_watch watch={owner,s->id,1};if(cb_invoke(&client,io,(struct cb_interface_id)CB_IO_INTERFACE_ID,CB_IO_OWNER_WATCH,&watch,sizeof(watch),0)!=CB_OK){surfaces=s->next;if(buffer)release(buffer);release(s);return CB_RESOURCE_FAILURE;}cb_copy(out,&s->id,8);*n=8;return CB_OK;
    }
    if(bytes<8)return CB_BAD_REQUEST;
    uint64_t id;cb_copy(&id,p,8);struct surface *s=find(id,owner);if(!s)return CB_DENIED;
    if(op==CB_GRAPHICS_SURFACE_RELEASE && bytes==8){if(!id)return CB_DENIED;destroy(s);return CB_OK;}
    if(op==CB_GRAPHICS_SURFACE_INFO && bytes==8){struct gfx_surface snapshot=s->raster;if(s==&screen){if(pending_copy.active)gfx_damage(&snapshot,pending_copy.damage);if(scene_copy.active)gfx_damage(&snapshot,scene_copy.viewport);for(unsigned i=0;i<scene_count;i++)gfx_damage(&snapshot,scene_damage[i]);snapshot.generation=scene_generation>UINT64_MAX-snapshot.generation?UINT64_MAX:snapshot.generation+scene_generation;}struct gfx_surface *r=&snapshot;struct cb_graphics_surface_info info={id,r->generation,r->width,r->height,r->damage.x,r->damage.y,r->damage.width,r->damage.height,r->dirty,0};cb_copy(out,&info,sizeof(info));*n=sizeof(info);return CB_OK;}
    if(op==CB_GRAPHICS_PRESENT && bytes==8)return id?CB_DENIED:present();
    if(s->container)return CB_BAD_REQUEST;
    if(op==CB_GRAPHICS_STATE && bytes==sizeof(struct cb_graphics_state)){struct cb_graphics_state state;cb_copy(&state,p,sizeof(state));if(state.reserved || state.composition>GFX_SOURCE_OVER || state.clip_width<0 || state.clip_height<0 || !gfx_transform(&s->raster,state.matrix))return CB_BAD_REQUEST;gfx_clip(&s->raster,(struct gfx_rect){state.clip_x,state.clip_y,state.clip_width,state.clip_height});s->raster.mode=state.composition;return s==&screen?save_context(owner):CB_OK;}
    if(op>=CB_GRAPHICS_LINE && op<=CB_GRAPHICS_PATH && bytes>=sizeof(struct cb_graphics_draw)){struct cb_graphics_draw d;cb_copy(&d,p,sizeof(d));if(d.fill>1 || (op!=CB_GRAPHICS_PATH && bytes!=sizeof(d)))return CB_BAD_REQUEST;struct gfx_point points[4];for(unsigned i=0;i<4;++i){if(d.x[i]<-32768 || d.x[i]>32767 || d.y[i]<-32768 || d.y[i]>32767){if(op!=CB_GRAPHICS_ARC || i!=2)return CB_BAD_REQUEST;}points[i]=(struct gfx_point){d.x[i],d.y[i]};}
        struct gfx_rect rect={d.x[0],d.y[0],d.x[1],d.y[1]};
        if(op==CB_GRAPHICS_LINE)gfx_line(&s->raster,points[0],points[1],d.color);
        else if(op==CB_GRAPHICS_RECTANGLE){gfx_rect(&s->raster,rect,d.color,d.fill);
            if((d.color>>24)==255 && d.fill && s->raster.matrix[0]==65536 && !s->raster.matrix[1] && !s->raster.matrix[2] && s->raster.matrix[3]==65536 && !s->raster.matrix[4] && !s->raster.matrix[5] && rect.x<=0 && rect.y<=0 && rect.width>=(int32_t)s->raster.width-rect.x && rect.height>=(int32_t)s->raster.height-rect.y && s->raster.clip.x==0 && s->raster.clip.y==0 && s->raster.clip.width==(int32_t)s->raster.width && s->raster.clip.height==(int32_t)s->raster.height)s->opaque=1;
        }
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
    if(op==CB_GRAPHICS_BLIT && bytes==sizeof(struct cb_graphics_blit)){struct cb_graphics_blit b;cb_copy(&b,p,sizeof(b));struct surface *source=find(b.source,owner);if(!source || source->container || b.reserved || b.opacity>255 || b.source_x<0 || b.source_y<0 || (uint32_t)b.source_x>source->raster.width || (uint32_t)b.source_y>source->raster.height || b.width>source->raster.width-b.source_x || b.height>source->raster.height-b.source_y || b.x<-32768 || b.x>32767 || b.y<-32768 || b.y>32767)return CB_BAD_REQUEST;if(!b.width || !b.height)return CB_OK;if(source==s && s==&screen && !scene_has_roots() && !pending_copy.active && !s->raster.dirty){
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
/* Capture pixel changes after every decoder path, without consuming the
 * public offscreen damage/generation observations. */
static cb_status_t operation(uint64_t owner,unsigned op,const unsigned char *p,size_t bytes,void *out,size_t *n){
    scene_collect();
    if(op!=CB_GRAPHICS_PRESENT && op!=CB_GRAPHICS_INFO && op!=CB_GRAPHICS_FONT_INFO && op!=CB_GRAPHICS_SURFACE_INFO && screen_access(op,p,bytes))scene_cancel_copy();
    struct surface *target=NULL;struct gfx_rect previous_damage={0};unsigned previous_dirty=0;
    if(bytes>=8 && ((op>=CB_GRAPHICS_LINE && op<=CB_GRAPHICS_BLIT) || op==CB_GRAPHICS_TEXT_BATCH)){
        uint64_t id;cb_copy(&id,p,8);target=id?find(id,owner):NULL;
        if(target && !target->container){previous_damage=target->raster.damage;previous_dirty=target->raster.dirty;gfx_reset_damage(&target->raster);if(op!=CB_GRAPHICS_TEXT_BATCH)target->opaque=0;}
        else target=NULL;
    }
    cb_status_t status=decode_operation(owner,op,p,bytes,out,n);
#ifdef CB_GRAPHICS_OPENGL
    if(target && target->raster.dirty)gl_damage_surface(target,target->raster.damage);
#endif
    if(target){if(target->raster.dirty)scene_damage_local(target,target->raster.damage);target->seen_generation=target->raster.generation;if(previous_dirty)gfx_damage(&target->raster,previous_damage);}
    scene_collect();return status;
}
__attribute__((noreturn)) void _start(const struct cb_startup *startup){
    const void *font_data;size_t font_bytes;CB_REQUIRE(cb_startup_find(startup,startup->bytes,"UbuntuMono-R.ttf",&font_data,&font_bytes) && font_engine_open(font_data,font_bytes));io=cb_acquire(CB_IO_NAME,(struct cb_interface_id)CB_IO_INTERFACE_ID);display=cb_acquire(CB_DISPLAY_NAME,(struct cb_interface_id)CB_DISPLAY_INTERFACE_ID);memory=cb_acquire(CB_MEMORY_NAME,(struct cb_interface_id)CB_MEMORY_INTERFACE_ID);CB_REQUIRE(cb_invoke(&client,display,(struct cb_interface_id)CB_DISPLAY_INTERFACE_ID,CB_DISPLAY_INFO,NULL,0,0)==CB_OK);struct cb_display_info info;cb_copy(&info,cb_result(&client),sizeof(info));CB_REQUIRE(info.width && info.height && info.width<=32767 && info.height<=32767);uint32_t *buffer=allocate((size_t)info.width*info.height*4);CB_REQUIRE(buffer);gfx_init(&screen.raster,buffer,info.width,info.height);gfx_damage(&screen.raster,(struct gfx_rect){0,0,info.width,info.height});
#ifdef CB_GRAPHICS_OPENGL
    CB_REQUIRE(gl_initialize()==CB_OK);
#endif
    static const char *names[]={"info","fill","glyph","surface_create","surface_release","surface_info","state","line","rectangle","ellipse","arc","bezier","path","bitmap","blit","present","glyphs","font_info","text_batch","surface_place","surface_scroll"};
    static const char *overlay_names[]={"info","set","move","hide"};cb_handle_t overlay_pub=cb_publish(&client,CB_GRAPHICS_OVERLAY_NAME,(struct cb_interface_id)CB_GRAPHICS_OVERLAY_INTERFACE_ID,overlay_names,4);
    /* /@graphics is the boot readiness publication; publish its overlay first. */
    cb_handle_t pub=cb_publish(&client,CB_GRAPHICS_NAME,(struct cb_interface_id)CB_GRAPHICS_INTERFACE_ID,names,21);
    for(;;){uint64_t bytes;CB_REQUIRE(cb_system_receive(input,sizeof(input),&bytes,0)==CB_OK && bytes>=sizeof(struct cb_delivery_header));struct cb_delivery_header d;cb_copy(&d,input,sizeof(d));if(d.call.flags&CB_CALL_SYSTEM_EVENT){if(d.call.operation==CB_IO_OWNER_EXIT && d.call.payload_bytes==8){struct surface *s=surfaces;while(s){struct surface *next=s->next;if(s->id==d.call.handle)destroy(s);s=next;}struct screen_context *c=contexts;while(c){struct screen_context *next=c->next;if(c->token==d.call.handle)drop_context(c);c=next;}struct overlay *l=overlays;while(l){struct overlay *next=l->next;if(l->token==d.call.handle)overlay_drop(l);l=next;}(void)present();continue;}if(d.call.operation==CB_HANDLE_RELEASE){while(overlays)overlay_drop(overlays);while(surfaces)destroy(surfaces);while(contexts)drop_context(contexts);if(scene_cache.pixels){release(scene_cache.pixels);release(scene_work.pixels);}
#ifdef CB_GRAPHICS_OPENGL
    gl_shutdown();
#endif
    release(screen.raster.pixels);font_engine_close();cb_system_release(overlay_pub);cb_system_release(pub);cb_system_release(display);cb_system_release(memory);cb_exit(0);}continue;}
        struct cb_admission caller;CB_REQUIRE(d.context_bytes<=bytes-sizeof(d) && d.call.payload_bytes==bytes-sizeof(d)-d.context_bytes);cb_status_t status=CB_DENIED;size_t n=0;unsigned char out[sizeof(struct cb_graphics_font)];if(cb_admission_parse(input+sizeof(d),d.context_bytes,&caller)){if(d.call.handle==pub)status=operation(caller.process,d.call.operation,input+sizeof(d)+d.context_bytes,d.call.payload_bytes,out,&n);else if(d.call.handle==overlay_pub)status=overlay_call(caller.process,d.call.operation,input+sizeof(d)+d.context_bytes,d.call.payload_bytes,out,&n);}CB_REQUIRE(cb_respond(&reply,&d,status,out,n)==CB_OK);
    }
}
