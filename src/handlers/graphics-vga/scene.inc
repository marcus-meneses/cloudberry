/* Retained, caller-owned child surfaces. Containers carry geometry/clipping,
 * bitmap leaves retain their pixels. No kernel/GPU/terminal policy lives here. */
static struct surface *scene_roots;
static struct gfx_surface scene_cache,scene_work;
static uint64_t scene_owner,scene_generation;
static unsigned scene_valid,scene_count,scene_retry;
#ifndef CB_SCENE_PIXEL_CHECK
#define CB_SCENE_PIXEL_CHECK() ((void)0)
#endif
static struct gfx_rect scene_damage[64];
static struct {unsigned active;struct cb_display_copy box;struct gfx_rect viewport;} scene_copy;
static struct gfx_surface *scene_output(void){return scene_cache.pixels?&scene_cache:&screen.raster;}
static struct gfx_rect scene_intersection(struct gfx_rect a,struct gfx_rect b){
    int32_t right=a.x+a.width<b.x+b.width?a.x+a.width:b.x+b.width,bottom=a.y+a.height<b.y+b.height?a.y+a.height:b.y+b.height;
    if(a.x<b.x)a.x=b.x;
    if(a.y<b.y)a.y=b.y;
    a.width=right>a.x?right-a.x:0;a.height=bottom>a.y?bottom-a.y:0;return a;
}
static void scene_damage_add(struct gfx_rect r){
    r=scene_intersection(r,(struct gfx_rect){0,0,screen.raster.width,screen.raster.height});if(!r.width || !r.height)return;
    if(scene_generation!=UINT64_MAX)++scene_generation;
    for(unsigned i=0;i<scene_count;++i){struct gfx_rect *p=&scene_damage[i];if(p->y==r.y && p->height==r.height && p->x<=r.x+r.width && r.x<=p->x+p->width){int32_t right=p->x+p->width>r.x+r.width?p->x+p->width:r.x+r.width;if(r.x<p->x)p->x=r.x;p->width=right-p->x;return;}if(p->x==r.x && p->width==r.width && p->y<=r.y+r.height && r.y<=p->y+p->height){int32_t bottom=p->y+p->height>r.y+r.height?p->y+p->height:r.y+r.height;if(r.y<p->y)p->y=r.y;p->height=bottom-p->y;return;}if(p->x<=r.x && p->y<=r.y && p->x+p->width>=r.x+r.width && p->y+p->height>=r.y+r.height)return;}
    if(scene_count==64){scene_count=1;scene_damage[0]=(struct gfx_rect){0,0,screen.raster.width,screen.raster.height};return;}
    scene_damage[scene_count++]=r;
}
/* Iterative ancestry, not recursive calls: arbitrary depth cannot exhaust the
 * provider stack. Coordinates remain checked even for deeply nested layouts. */
static int scene_bounds(struct surface *s,struct gfx_rect *out,int64_t *origin_x,int64_t *origin_y){
    if(!s || s->owner!=scene_owner)return 0;
    int64_t x=0,y=0;for(struct surface *p=s;p;p=p->parent){if(!p->attached || !p->visible || __builtin_add_overflow(x,p->x,&x) || __builtin_add_overflow(y,p->y,&y))return 0;}
    *origin_x=x;*origin_y=y;
    int64_t left=0,top=0,right=screen.raster.width,bottom=screen.raster.height;
    for(struct surface *p=s;p;p=p->parent){if(x>left)left=x;if(y>top)top=y;if(x+p->raster.width<right)right=x+p->raster.width;if(y+p->raster.height<bottom)bottom=y+p->raster.height;x-=p->x;y-=p->y;}
    if(right<=left || bottom<=top)return 0;
    *out=(struct gfx_rect){left,top,right-left,bottom-top};return 1;
}
static void scene_damage_surface(struct surface *s){struct gfx_rect r;int64_t x,y;if(scene_bounds(s,&r,&x,&y))scene_damage_add(r);}
static void scene_damage_local(struct surface *s,struct gfx_rect local){struct gfx_rect bounds;int64_t x,y;if(!scene_bounds(s,&bounds,&x,&y))return;int64_t left=x+local.x,top=y+local.y,right=left+local.width,bottom=top+local.height;if(left<bounds.x)left=bounds.x;if(top<bounds.y)top=bounds.y;if(right>(int64_t)bounds.x+bounds.width)right=(int64_t)bounds.x+bounds.width;if(bottom>(int64_t)bounds.y+bounds.height)bottom=(int64_t)bounds.y+bounds.height;if(right>left && bottom>top)scene_damage_add((struct gfx_rect){left,top,right-left,bottom-top});}
static void scene_forget_presented(struct gfx_rect r){for(unsigned i=0;i<scene_count;){struct gfx_rect p=scene_damage[i];if(r.x<=p.x && r.y<=p.y && r.x+r.width>=p.x+p.width && r.y+r.height>=p.y+p.height)scene_damage[i]=scene_damage[--scene_count];else ++i;}}
static void scene_cancel_copy(void){if(scene_copy.active){scene_damage_add(scene_copy.viewport);scene_copy.active=0;}}
static int scene_has_roots(void){for(struct surface *p=scene_roots;p;p=p->sibling)if(p->owner==scene_owner && p->visible)return 1;return 0;}
static void scene_select(uint64_t owner){
    if(owner==scene_owner)return;
    scene_cancel_copy();scene_owner=owner;
    if(pending_copy.active){gfx_damage(&screen.raster,pending_copy.damage);pending_copy.active=0;}
#ifdef CB_GRAPHICS_OPENGL
    scene_damage_add((struct gfx_rect){0,0,screen.raster.width,screen.raster.height});
#endif
    if(scene_cache.pixels){scene_damage_add((struct gfx_rect){0,0,screen.raster.width,screen.raster.height});}
}
static cb_status_t scene_allocate(void){
#ifdef CB_GRAPHICS_OPENGL
    return CB_OK; /* GPU composition needs no full-screen CPU work/cache pair. */
#endif
    if(scene_cache.pixels)return CB_OK;
    size_t bytes=(size_t)screen.raster.width*screen.raster.height*4;uint32_t *p=allocate(bytes);if(!p)return CB_RESOURCE_FAILURE;
    uint32_t *work=allocate(bytes);if(!work){release(p);return CB_RESOURCE_FAILURE;}memcpy(p,screen.raster.pixels,bytes);gfx_init(&scene_cache,p,screen.raster.width,screen.raster.height);gfx_init(&scene_work,work,screen.raster.width,screen.raster.height);return CB_OK;
}
static void scene_unlink(struct surface *s){
    scene_damage_surface(s);if(!s->attached)return;
    struct surface **at=s->parent?&s->parent->child:&scene_roots;
    while(*at && *at!=s)at=&(*at)->sibling;
    if(*at)*at=s->sibling;
    if(s->parent && s->parent->last_child==s){s->parent->last_child=s->parent->child;while(s->parent->last_child && s->parent->last_child->sibling)s->parent->last_child=s->parent->last_child->sibling;}
    s->parent=NULL;s->sibling=NULL;s->attached=0;
}
static void scene_link(struct surface *s,struct surface *parent){
    s->parent=parent;s->attached=1;s->sibling=NULL;
    if(parent){if(parent->last_child)parent->last_child->sibling=s;else parent->child=s;parent->last_child=s;}
    else {struct surface **at=&scene_roots;while(*at)at=&(*at)->sibling;*at=s;}
}
static struct cb_graphics_place scene_placement(const unsigned char *p,size_t i){struct cb_graphics_place q;cb_copy(&q,p+i*sizeof(q),sizeof(q));return q;}
static cb_status_t scene_place(uint64_t owner,const unsigned char *p,size_t bytes){
    if(!bytes || bytes%sizeof(struct cb_graphics_place))return CB_BAD_REQUEST;
    size_t count=bytes/sizeof(struct cb_graphics_place);
    for(size_t i=0;i<count;++i){struct cb_graphics_place q=scene_placement(p,i);struct surface *s=find(q.surface,owner),*parent=find(q.parent,owner);if(!s || !q.surface || !parent)return CB_DENIED;if(q.reserved || q.flags&~CB_SURFACE_VISIBLE || q.x<-32768 || q.x>32767 || q.y<-32768 || q.y>32767)return CB_BAD_REQUEST;
        for(size_t j=0;j<i;++j)if(scene_placement(p,j).surface==q.surface)return CB_BAD_REQUEST;
        /* Follow the proposed graph, including parents elsewhere in this batch. */
        uint64_t id=q.parent;size_t steps=0,total=0;for(struct surface *n=surfaces;n;n=n->next)++total;
        while(id){if(id==q.surface || ++steps>total)return CB_BAD_REQUEST;struct surface *n=find(id,owner);if(!n)return CB_DENIED;uint64_t next=n->attached && n->parent?n->parent->id:0;for(size_t j=0;j<count;++j){struct cb_graphics_place other=scene_placement(p,j);if(other.surface==id){next=other.parent;break;}}id=next;}
    }
    cb_status_t status=scene_allocate();if(status)return status;
    /* Detach changed edges first: even an acyclic final transaction may form
     * a temporary cycle if reparentings are applied one at a time. */
    for(size_t i=0;i<count;++i){struct cb_graphics_place q=scene_placement(p,i);struct surface *s=find(q.surface,owner),*parent=q.parent?find(q.parent,owner):NULL;if(s->attached && s->parent==parent && s->x==q.x && s->y==q.y && s->visible==!!(q.flags&CB_SURFACE_VISIBLE))continue;s->layout_dirty=1;scene_damage_surface(s);if(s->attached && s->parent!=parent)scene_unlink(s);}
    for(size_t i=0;i<count;++i){struct cb_graphics_place q=scene_placement(p,i);struct surface *s=find(q.surface,owner),*parent=q.parent?find(q.parent,owner):NULL;if(!s->attached)scene_link(s,parent);s->x=q.x;s->y=q.y;s->visible=!!(q.flags&CB_SURFACE_VISIBLE);}
    /* New damage was recorded above for moved nodes; roots/descendants that
     * were previously hidden are collected below from their changed geometry. */
    for(size_t i=0;i<count;++i){struct cb_graphics_place q=scene_placement(p,i);struct surface *n=find(q.surface,owner);if(n->layout_dirty){scene_damage_surface(n);n->layout_dirty=0;}}
    return CB_OK;
}
static void scene_collect(void){for(struct surface *s=surfaces;s;s=s->next)if(s->raster.generation!=s->seen_generation){
#ifdef CB_GRAPHICS_OPENGL
    if(s->raster.dirty)gl_damage_surface(s,s->raster.damage);
#endif
    scene_damage_surface(s);s->seen_generation=s->raster.generation;}}
static struct surface *scene_next(struct surface *s){if(s->child)return s->child;while(s && !s->sibling)s=s->parent;return s?s->sibling:NULL;}
static uint32_t scene_over(uint32_t source,uint32_t dest){unsigned a=source>>24;if(a==255)return source;if(!a)return dest;uint32_t result=0xff000000;for(unsigned s=0;s<24;s+=8)result|=((((source>>s)&255)*a+((dest>>s)&255)*(255-a)+127)/255)<<s;return result;}
static struct gfx_rect scene_compose(struct gfx_rect r){
    if(!scene_work.pixels)return r;
    for(int32_t y=r.y;y<r.y+r.height;++y)for(int32_t x=r.x;x<r.x+r.width;++x)scene_work.pixels[(size_t)y*scene_work.width+x]=0xff000000|gfx_rgb(screen.raster.pixels[(size_t)y*screen.raster.width+x]);
    for(struct surface *s=scene_roots;s;s=scene_next(s)){struct gfx_rect bounds;int64_t ox,oy;if(s->container || !scene_bounds(s,&bounds,&ox,&oy))continue;bounds=scene_intersection(bounds,r);if(!bounds.width || !bounds.height)continue;
        for(int32_t y=bounds.y;y<bounds.y+bounds.height;++y){uint32_t *dest=scene_work.pixels+(size_t)y*scene_work.width+bounds.x;const uint32_t *source=s->raster.pixels+(size_t)(y-oy)*s->raster.width+(bounds.x-ox);if(s->opaque)memcpy(dest,source,(size_t)bounds.width*4);else for(int32_t x=0;x<bounds.width;++x)dest[x]=scene_over(source[x],dest[x]);}
    }
    /* A failed transfer may leave cache pixels ahead of the display. Force
     * repair on retry; normal layout/content damage uses pixel comparison. */
    if(scene_retry){for(int32_t y=r.y;y<r.y+r.height;++y)memcpy(scene_cache.pixels+(size_t)y*scene_cache.width+r.x,scene_work.pixels+(size_t)y*scene_work.width+r.x,(size_t)r.width*4);return r;}
    struct gfx_rect changed={0};int32_t left=r.x+r.width,top=r.y+r.height,right=r.x,bottom=r.y;
    for(int32_t y=r.y;y<r.y+r.height;++y)for(int32_t x=r.x;x<r.x+r.width;++x){CB_SCENE_PIXEL_CHECK();size_t at=(size_t)y*scene_cache.width+x;uint32_t value=scene_work.pixels[at];if(scene_cache.pixels[at]==value)continue;scene_cache.pixels[at]=value;if(x<left)left=x;if(y<top)top=y;if(x+1>right)right=x+1;if(y+1>bottom)bottom=y+1;}
    if(right>left && bottom>top)changed=(struct gfx_rect){left,top,right-left,bottom-top};
    return changed;
}
/* A fast scroll is valid only for a fully opaque, nonoverlapping set of direct
 * leaves covering a container, without any intersecting unrelated surfaces. */
static int scene_scroll_cover(struct surface *parent,struct gfx_rect viewport){
    if(!parent || !parent->container)return 0;
    uint64_t area=0;
    for(struct surface *s=surfaces;s;s=s->next){struct gfx_rect r;int64_t x,y;if(!scene_bounds(s,&r,&x,&y) || !intersects(r,viewport))continue;if(s==parent)continue;
        int ancestor=0;for(struct surface *p=parent->parent;p;p=p->parent)if(s==p)ancestor=1;if(ancestor)continue;
        if(s->parent!=parent || s->container || !s->opaque || s->child)return 0;
        r=scene_intersection(r,viewport);area+=(uint64_t)r.width*r.height;
        for(struct surface *t=s->sibling;t;t=t->sibling){struct gfx_rect q;int64_t tx,ty;if(scene_bounds(t,&q,&tx,&ty) && intersects(r,q))return 0;}
    }return area==(uint64_t)viewport.width*viewport.height;
}
static cb_status_t scene_scroll(uint64_t owner,const unsigned char *p,size_t bytes){
    if(bytes!=sizeof(struct cb_graphics_scroll))return CB_BAD_REQUEST;
    struct cb_graphics_scroll q;cb_copy(&q,p,sizeof(q));struct surface *parent=find(q.surface,owner);if(!parent || !q.surface)return CB_DENIED;
    for(struct surface *s=parent->child;s;s=s->sibling)if((int64_t)s->x+q.dx<-32768 || (int64_t)s->x+q.dx>32767 || (int64_t)s->y+q.dy<-32768 || (int64_t)s->y+q.dy>32767)return CB_BAD_REQUEST;
    if(!q.dx && !q.dy)return CB_OK;
    struct gfx_rect viewport={0};int64_t x,y;int visible=scene_bounds(parent,&viewport,&x,&y);
    int fast=visible && scene_work.pixels && scene_valid && !scene_copy.active && !scene_count && !screen.raster.dirty && !pending_copy.active && scene_scroll_cover(parent,viewport) && q.dx>-viewport.width && q.dx<viewport.width && q.dy>-viewport.height && q.dy<viewport.height;
    if(scene_copy.active)scene_cancel_copy();
    if(fast){struct gfx_rect moved=viewport;moved.x+=q.dx;moved.y+=q.dy;struct gfx_rect dest=scene_intersection(viewport,moved);scene_copy.active=1;scene_copy.viewport=viewport;scene_copy.box=(struct cb_display_copy){dest.x,dest.y,dest.x-q.dx,dest.y-q.dy,dest.width,dest.height};
        scene_damage_add((struct gfx_rect){viewport.x,viewport.y,viewport.width,dest.y-viewport.y});scene_damage_add((struct gfx_rect){viewport.x,dest.y+dest.height,viewport.width,viewport.y+viewport.height-dest.y-dest.height});scene_damage_add((struct gfx_rect){viewport.x,dest.y,dest.x-viewport.x,dest.height});scene_damage_add((struct gfx_rect){dest.x+dest.width,dest.y,viewport.x+viewport.width-dest.x-dest.width,dest.height});
    }else if(visible)scene_damage_add(viewport);
    for(struct surface *s=parent->child;s;s=s->sibling){s->x+=q.dx;s->y+=q.dy;}
    return CB_OK;
}
static cb_status_t scene_apply_copy(void){
    struct cb_display_copy b=scene_copy.box;struct gfx_rect viewport=scene_copy.viewport;scene_copy.active=0;
    cb_status_t status=cb_invoke(&client,display,(struct cb_interface_id)CB_DISPLAY_INTERFACE_ID,CB_DISPLAY_COPY,&b,sizeof(b),0);
    if(status){scene_retry=1;scene_damage_add(viewport);return status==CB_UNAVAILABLE?CB_OK:status;}
    for(unsigned i=0;i<b.height;++i){unsigned row=b.y>b.source_y?b.height-1-i:i;memmove(scene_cache.pixels+(size_t)(b.y+row)*scene_cache.width+b.x,scene_cache.pixels+(size_t)(b.source_y+row)*scene_cache.width+b.source_x,(size_t)b.width*4);}
    overlay_copy_repair(&b);return CB_OK;
}
