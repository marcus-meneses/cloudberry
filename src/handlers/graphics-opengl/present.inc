/* Content is rasterized once by the shared decoder/FreeType implementation.
 * The GPU owns cached BGRA copies and composes clipped hierarchy/overlay layers.
 * Moving a surface changes only its placement, never its pixel upload. */
#include "../../interfaces/render-backend.h"
struct gl_cache {struct gl_cache *next;const void *key;uint64_t id,generation;uint32_t width,height;unsigned valid;};
static struct gl_cache *gl_caches;
static cb_handle_t gl_backend;
static unsigned gl_valid;
static unsigned char gl_upload[CB_FRAME_LIMIT-1024],gl_frame[CB_FRAME_LIMIT-1024];
static size_t gl_count;static unsigned gl_first,gl_partial;
static cb_status_t gl_invoke(unsigned operation,const void *p,size_t n){return cb_invoke(&client,gl_backend,(struct cb_interface_id)CB_RENDER_BACKEND_INTERFACE_ID,operation,p,n,0);}
static cb_status_t gl_initialize(void){
    gl_backend=cb_acquire(CB_RENDER_BACKEND_NAME,(struct cb_interface_id)CB_RENDER_BACKEND_INTERFACE_ID);
    cb_status_t s=gl_invoke(CB_RENDER_BACKEND_INFO,NULL,0);if(s)return s;
    struct cb_reply_header h;cb_copy(&h,client.result,sizeof(h));if(h.result_bytes!=sizeof(struct cb_display_info))return CB_BAD_REQUEST;
    struct cb_display_info info;cb_copy(&info,cb_result(&client),sizeof(info));return info.width==screen.raster.width && info.height==screen.raster.height?CB_OK:CB_BAD_REQUEST;
}
static void gl_drop_cache(const void *key){
    struct gl_cache **at=&gl_caches;while(*at && (*at)->key!=key)at=&(*at)->next;if(!*at)return;
    struct gl_cache *c=*at;CB_REQUIRE(gl_invoke(CB_RENDER_BACKEND_RELEASE,&c->id,8)==CB_OK);*at=c->next;release(c);
}
static void gl_shutdown(void){while(gl_caches)gl_drop_cache(gl_caches->key);if(gl_backend)cb_system_release(gl_backend);gl_backend=0;}
static void gl_damage_surface(struct surface *s,struct gfx_rect rect){
    struct gfx_surface damage={.dirty=s->upload_dirty,.damage=s->upload_damage};gfx_damage(&damage,rect);s->upload_dirty=damage.dirty;s->upload_damage=damage.damage;
}
static cb_status_t gl_patch(uint64_t id,const uint32_t *pixels,uint32_t stride,struct gfx_rect rect){
    size_t capacity=sizeof(gl_upload)-sizeof(struct cb_render_patch);if(capacity>12288)capacity=12288;
    size_t row=(size_t)rect.width*4,total=row*rect.height;
    for(size_t offset=0;offset<total;){
        size_t rows=(total-offset)/row,limit=capacity/row;if(rows>limit)rows=limit;
        size_t n=rows*row;struct cb_render_patch q={id,offset,rect.x,rect.y,rect.width,rect.height,n,(!offset?CB_RENDER_WRITE_BEGIN:0)|(offset+n==total?CB_RENDER_WRITE_COMMIT:0)};
        cb_copy(gl_upload,&q,sizeof(q));
        for(size_t y=0;y<rows;++y)cb_copy(gl_upload+sizeof(q)+y*row,pixels+(size_t)(rect.y+offset/row+y)*stride+rect.x,row);
        cb_status_t status=gl_invoke(CB_RENDER_BACKEND_PATCH,gl_upload,sizeof(q)+n);if(status)return status;offset+=n;
    }
    return CB_OK;
}
static cb_status_t gl_sync(const void *key,const uint32_t *pixels,uint32_t width,uint32_t height,uint64_t generation,const struct gfx_rect *damage,uint64_t *id){
    struct gl_cache *c=gl_caches;while(c && c->key!=key)c=c->next;
    if(!c){
        c=allocate(sizeof(*c));if(!c)return CB_RESOURCE_FAILURE;*c=(struct gl_cache){.key=key,.width=width,.height=height};
        struct cb_render_create q={width,height};cb_status_t s=gl_invoke(CB_RENDER_BACKEND_CREATE,&q,sizeof(q));if(s){release(c);return s;}
        struct cb_reply_header h;cb_copy(&h,client.result,sizeof(h));if(h.result_bytes!=8){release(c);return CB_BAD_REQUEST;}
        cb_copy(&c->id,cb_result(&client),8);if(!c->id){release(c);return CB_BAD_REQUEST;}c->next=gl_caches;gl_caches=c;
    }
    if(c->width!=width || c->height!=height)return CB_BAD_REQUEST;
    if(!c->valid || c->generation!=generation || generation==UINT64_MAX){
        size_t total=(size_t)width*height*4;size_t capacity=sizeof(gl_upload)-sizeof(struct cb_render_write);if(capacity>12288)capacity=12288;
        int patch=c->valid && damage && damage->width>0 && damage->height>0 && (uint32_t)damage->width<=capacity/4 && (uint64_t)damage->width*damage->height<(uint64_t)width*height;
        c->valid=0; /* Failed partial uploads must recover with a full upload. */
        if(patch){cb_status_t status=gl_patch(c->id,pixels,width,*damage);if(status)return status;}
        else for(size_t at=0;at<total;){size_t n=total-at;if(n>capacity)n=capacity;
            struct cb_render_write q={c->id,at,n,(!at?CB_RENDER_WRITE_BEGIN:0)|(at+n==total?CB_RENDER_WRITE_COMMIT:0)};
            cb_copy(gl_upload,&q,sizeof(q));cb_copy(gl_upload+sizeof(q),(const unsigned char *)pixels+at,n);
            cb_status_t s=gl_invoke(CB_RENDER_BACKEND_WRITE,gl_upload,sizeof(q)+n);if(s)return s;at+=n;
        }
        c->generation=generation;c->valid=1;
    }
    *id=c->id;return CB_OK;
}
static cb_status_t gl_flush(unsigned end){
    if(!gl_count)return CB_BAD_REQUEST;
    struct cb_render_batch h={.version={sizeof(h),1,0},.count=gl_count,.flags=(gl_first?(CB_RENDER_COMPOSE_BEGIN|(gl_partial?CB_RENDER_COMPOSE_DAMAGE:0)):0)|(end?CB_RENDER_COMPOSE_END:0)};
    cb_copy(gl_frame,&h,sizeof(h));cb_status_t s=gl_invoke(CB_RENDER_BACKEND_COMPOSE,gl_frame,sizeof(h)+gl_count*sizeof(struct cb_render_layer));if(!s){gl_count=0;gl_first=0;}return s;
}
static cb_status_t gl_layer(struct cb_render_layer layer){
    if(gl_count==(sizeof(gl_frame)-sizeof(struct cb_render_batch))/sizeof(layer)){cb_status_t s=gl_flush(0);if(s)return s;}
    cb_copy(gl_frame+sizeof(struct cb_render_batch)+gl_count*sizeof(layer),&layer,sizeof(layer));++gl_count;return CB_OK;
}
static cb_status_t gl_present(void){
    scene_collect();
    if(!gl_valid)gfx_damage(&screen.raster,(struct gfx_rect){0,0,screen.raster.width,screen.raster.height});
    if(pending_copy.active){gfx_damage(&screen.raster,pending_copy.damage);pending_copy.active=0;}
    if(!screen.raster.dirty && !scene_count && !scene_retry && !overlay_damage_count)return CB_OK;
    struct gfx_surface changed={0};
    if(!gl_valid || scene_retry)gfx_damage(&changed,(struct gfx_rect){0,0,screen.raster.width,screen.raster.height});
    else {
        if(screen.raster.dirty)gfx_damage(&changed,screen.raster.damage);
        for(unsigned i=0;i<scene_count;++i)gfx_damage(&changed,scene_damage[i]);
        for(unsigned i=0;i<overlay_damage_count;++i)gfx_damage(&changed,overlay_damage[i]);
    }
    struct gfx_rect damaged=scene_intersection(changed.damage,(struct gfx_rect){0,0,screen.raster.width,screen.raster.height});
    if(!damaged.width || !damaged.height)damaged=(struct gfx_rect){0,0,screen.raster.width,screen.raster.height};
    gl_partial=damaged.width!=(int32_t)screen.raster.width || damaged.height!=(int32_t)screen.raster.height;
    uint64_t id;cb_status_t s=gl_sync(&screen,screen.raster.pixels,screen.raster.width,screen.raster.height,screen.raster.generation,screen.raster.dirty?&screen.raster.damage:NULL,&id);if(s)return s;
    gl_count=0;gl_first=1;
    s=gl_layer((struct cb_render_layer){.surface=id,.x=damaged.x,.y=damaged.y,.width=damaged.width,.height=damaged.height,.source_x=damaged.x,.source_y=damaged.y});if(s)return s;
    for(struct surface *n=scene_roots;n;n=scene_next(n)){
        struct gfx_rect bounds;int64_t x,y;if(n->container || !scene_bounds(n,&bounds,&x,&y))continue;
        bounds=scene_intersection(bounds,damaged);if(!bounds.width || !bounds.height)continue;
        s=gl_sync(n,n->raster.pixels,n->raster.width,n->raster.height,n->raster.generation,n->upload_dirty?&n->upload_damage:NULL,&id);if(s)return s;n->upload_dirty=0;
        s=gl_layer((struct cb_render_layer){id,bounds.x,bounds.y,bounds.width,bounds.height,bounds.x-x,bounds.y-y,n->opaque?0:CB_RENDER_LAYER_ALPHA,0});if(s)return s;
    }
    for(struct overlay *l=overlays;l;l=l->next)if(l->visible){
        struct gfx_rect b=scene_intersection(layer_rect(l),damaged);if(!b.width || !b.height)continue;
        s=gl_sync(l,l->pixels,l->width,l->height,1,NULL,&id);if(s)return s;
        s=gl_layer((struct cb_render_layer){id,b.x,b.y,b.width,b.height,b.x-l->x,b.y-l->y,CB_RENDER_LAYER_ALPHA,0});if(s)return s;
    }
    s=gl_flush(1);if(s){scene_retry=1;return s;}
    gfx_reset_damage(&screen.raster);scene_count=scene_retry=overlay_damage_count=0;scene_valid=gl_valid=1;sparse_active=sparse_count=0;return CB_OK;
}
