/* One independently owned layer per trusted provider, dynamically allocated.
 * Cursor shape/position policy lives in mouse, not in this compositor. */
struct overlay {struct overlay *next;uint64_t owner,token;int32_t x,y;uint32_t width,height;unsigned visible;uint32_t *pixels;};
static struct overlay *overlays;
static struct gfx_rect overlay_damage[2];static unsigned overlay_damage_count;
static int intersects(struct gfx_rect a,struct gfx_rect b){return a.x<(int64_t)b.x+b.width && b.x<(int64_t)a.x+a.width && a.y<(int64_t)b.y+b.height && b.y<(int64_t)a.y+a.height;}
static struct gfx_rect layer_rect(const struct overlay *l){return (struct gfx_rect){l->x,l->y,(int32_t)l->width,(int32_t)l->height};}
static void overlay_dirty(struct gfx_rect r){
    if(r.x<0){r.width+=r.x;r.x=0;}if(r.y<0){r.height+=r.y;r.y=0;}if((int64_t)r.x+r.width>screen.raster.width)r.width=(int32_t)screen.raster.width-r.x;if((int64_t)r.y+r.height>screen.raster.height)r.height=(int32_t)screen.raster.height-r.y;
    if(r.width<=0 || r.height<=0)return;
    if(overlay_damage_count<2)overlay_damage[overlay_damage_count++]=r;else {struct gfx_rect *p=&overlay_damage[0];int32_t right=p->x+p->width>r.x+r.width?p->x+p->width:r.x+r.width,bottom=p->y+p->height>r.y+r.height?p->y+p->height:r.y+r.height;if(r.x<p->x)p->x=r.x;if(r.y<p->y)p->y=r.y;p->width=right-p->x;p->height=bottom-p->y;}
}
/* A display copy can copy a composited layer along with its source. Repair
 * that translated ghost and the live layer from final authoritative backing. */
static void overlay_copy_repair(const struct cb_display_copy *b){
    struct gfx_rect source={(int32_t)b->source_x,(int32_t)b->source_y,(int32_t)b->width,(int32_t)b->height};
    for(struct overlay *l=overlays;l;l=l->next)if(l->visible){struct gfx_rect r=layer_rect(l);overlay_dirty(r);if(!intersects(source,r))continue;int32_t right=r.x+r.width,bottom=r.y+r.height;if(r.x<source.x)r.x=source.x;if(r.y<source.y)r.y=source.y;if(right>source.x+source.width)right=source.x+source.width;if(bottom>source.y+source.height)bottom=source.y+source.height;r.width=right-r.x;r.height=bottom-r.y;r.x+=(int32_t)b->x-source.x;r.y+=(int32_t)b->y-source.y;overlay_dirty(r);}
}
#ifndef CB_GRAPHICS_OPENGL
static int overlay_intersects(struct gfx_rect r){for(struct overlay *l=overlays;l;l=l->next)if(l->visible && intersects(r,layer_rect(l)))return 1;return 0;}
#endif
static uint32_t overlay_rgb(int32_t x,int32_t y,uint32_t rgb){
    for(struct overlay *l=overlays;l;l=l->next){if(!l->visible || x<l->x || y<l->y || (uint32_t)(x-l->x)>=l->width || (uint32_t)(y-l->y)>=l->height)continue;uint32_t p=l->pixels[(size_t)(y-l->y)*l->width+(x-l->x)],a=p>>24;if(!a)continue;uint32_t value=0;for(unsigned s=0;s<24;s+=8)value|=((((p>>s)&255)*a+((rgb>>s)&255)*(255-a)+127)/255)<<s;rgb=value;}return rgb;
}
static void overlay_drop(struct overlay *l){
#ifdef CB_GRAPHICS_OPENGL
    gl_drop_cache(l);
#endif
    if(l->visible)overlay_dirty(layer_rect(l));
    struct cb_io_owner_watch watch={l->owner,l->token,0};CB_REQUIRE(cb_invoke(&client,io,(struct cb_interface_id)CB_IO_INTERFACE_ID,CB_IO_OWNER_WATCH,&watch,sizeof(watch),0)==CB_OK);struct overlay **at=&overlays;while(*at!=l)at=&(*at)->next;*at=l->next;release(l->pixels);release(l);}
static cb_status_t overlay_call(uint64_t owner,unsigned op,const void *payload,size_t bytes,void *out,size_t *n){
    if(op==CB_OVERLAY_INFO && !bytes){struct cb_display_info info={screen.raster.width,screen.raster.height};cb_copy(out,&info,sizeof(info));*n=sizeof(info);return CB_OK;}
    struct overlay *l=overlays;while(l && l->owner!=owner)l=l->next;
    if(op==CB_OVERLAY_HIDE && !bytes){if(l && l->visible){overlay_dirty(layer_rect(l));l->visible=0;}return present_overlay();}
    if(op==CB_OVERLAY_MOVE && bytes==sizeof(struct cb_overlay_move)){if(!l)return CB_NOT_FOUND;struct cb_overlay_move q;cb_copy(&q,payload,sizeof(q));if(q.x<0 || q.y<0 || q.x>=(int32_t)screen.raster.width || q.y>=(int32_t)screen.raster.height)return CB_BAD_REQUEST;if(l->visible && l->x==q.x && l->y==q.y)return CB_OK;if(l->visible)overlay_dirty(layer_rect(l));l->x=q.x;l->y=q.y;l->visible=1;overlay_dirty(layer_rect(l));return present_overlay();}
    if(op!=CB_OVERLAY_SET || bytes<sizeof(struct cb_overlay_set))return CB_BAD_REQUEST;
    struct cb_overlay_set q;cb_copy(&q,payload,sizeof(q));if(q.x<0 || q.y<0 || q.x>=(int32_t)screen.raster.width || q.y>=(int32_t)screen.raster.height || !q.width || !q.height || q.width>256 || q.height>256 || (uint64_t)q.width*q.height*4!=bytes-sizeof(q))return CB_BAD_REQUEST;
    uint32_t *copy=allocate(bytes-sizeof(q));if(!copy)return CB_RESOURCE_FAILURE;cb_copy(copy,(const unsigned char *)payload+sizeof(q),bytes-sizeof(q));
    if(!l){if(serial==UINT64_MAX){release(copy);return CB_RESOURCE_FAILURE;}l=allocate(sizeof(*l));if(!l){release(copy);return CB_RESOURCE_FAILURE;}*l=(struct overlay){.owner=owner,.token=++serial};struct cb_io_owner_watch watch={owner,l->token,1};cb_status_t s=cb_invoke(&client,io,(struct cb_interface_id)CB_IO_INTERFACE_ID,CB_IO_OWNER_WATCH,&watch,sizeof(watch),0);if(s){release(copy);release(l);return s;}l->next=overlays;overlays=l;}
    if(l->visible)overlay_dirty(layer_rect(l));
#ifdef CB_GRAPHICS_OPENGL
    gl_drop_cache(l);
#endif
    if(l->pixels)release(l->pixels);
    l->pixels=copy;l->x=q.x;l->y=q.y;l->width=q.width;l->height=q.height;l->visible=1;overlay_dirty(layer_rect(l));return present_overlay();
}
