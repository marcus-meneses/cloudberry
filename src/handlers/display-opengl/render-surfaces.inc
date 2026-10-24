/* Private cached surface ownership. Native GPU IDs never leave the display. */
void cb_gl_display_heap(struct cb_gl_display *d,cb_gl_allocate allocate,cb_gl_release release,void *heap){if(d && !d->textures){d->allocate=allocate;d->release=release;d->heap=heap;}}
static struct cb_gl_texture *surface_find(struct cb_gl_display *d,uint64_t owner,uint64_t id){for(struct cb_gl_texture *t=d->textures;t;t=t->next)if(t->id==id && t->owner==owner)return t;return NULL;}
cb_status_t cb_gl_surface_create(struct cb_gl_display *d,uint64_t owner,uint32_t width,uint32_t height,uint64_t *id){
    cb_status_t s=live(d);if(s)return s;
    if(!owner || !id || !width || !height || width>32767 || height>32767 || width>UINT32_MAX/4/height)return CB_BAD_REQUEST;
    if(!d->allocate || !d->release || d->surface_serial==UINT64_MAX)return CB_RESOURCE_FAILURE;
    struct cb_gl_texture *t=d->allocate(d->heap,sizeof(*t));if(!t)return CB_RESOURCE_FAILURE;
    *t=(struct cb_gl_texture){.owner=owner,.id=++d->surface_serial,.width=width,.height=height,.bytes=(uint64_t)width*height*4};
    s=texture_format(d,width,height,1,0,&t->resource);
    if(s){d->release(d->heap,t);return s;} /* uncertain GPU state stays device-owned */
    t->next=d->textures;d->textures=t;*id=t->id;return CB_OK;
}
cb_status_t cb_gl_surface_release(struct cb_gl_display *d,uint64_t owner,uint64_t id){
    cb_status_t s=live(d);if(s)return s;struct cb_gl_texture *t=surface_find(d,owner,id);if(!t)return CB_BAD_HANDLE;
    if(d->composing)return CB_BUSY;
    s=call(d,(struct cb_io_gpu_request){.operation=CB_GPU_DESTROY,.resource=t->resource},NULL,0,NULL,0);if(s)return s;
    struct cb_gl_texture **at=&d->textures;while(*at!=t)at=&(*at)->next;*at=t->next;d->release(d->heap,t);return CB_OK;
}
cb_status_t cb_gl_surface_owner_exit(struct cb_gl_display *d,uint64_t owner){
    cb_status_t s=live(d);if(s)return s;
    if(d->composing && d->compose_owner==owner){d->composing=0;d->compose_owner=0;}
    for(struct cb_gl_texture *t=d->textures,*next;t;t=next){next=t->next;if(t->owner==owner){s=cb_gl_surface_release(d,owner,t->id);if(s)return s;}}
    return CB_OK;
}
cb_status_t cb_gl_surface_write(struct cb_gl_display *d,uint64_t owner,const void *payload,size_t bytes){
    cb_status_t s=live(d);if(s)return s;
    if(!payload || bytes<sizeof(struct cb_render_write))return CB_BAD_REQUEST;
    struct cb_render_write q;__builtin_memcpy(&q,payload,sizeof(q));
    if(q.flags&~(CB_RENDER_WRITE_BEGIN|CB_RENDER_WRITE_COMMIT) || q.bytes!=bytes-sizeof(q) || q.bytes>CB_GL_TRANSFER_BYTES)return CB_BAD_REQUEST;
    struct cb_gl_texture *t=surface_find(d,owner,q.surface);if(!t)return CB_BAD_HANDLE;
    uint64_t written=q.flags&CB_RENDER_WRITE_BEGIN?0:t->written;
    if((!(q.flags&CB_RENDER_WRITE_BEGIN) && t->writing!=1) || q.offset!=written || q.offset>t->bytes || q.bytes>t->bytes-q.offset || ((q.flags&CB_RENDER_WRITE_COMMIT) && q.offset+q.bytes!=t->bytes))return CB_BAD_REQUEST;
    if(q.flags&CB_RENDER_WRITE_BEGIN){t->written=0;t->writing=1;t->ready=0;}
    if(q.bytes){s=call(d,(struct cb_io_gpu_request){.operation=CB_GPU_WRITE,.resource=t->resource,.offset=q.offset,.bytes=q.bytes},(const unsigned char *)payload+sizeof(q),q.bytes,NULL,0);if(s)return s;}
    t->written+=q.bytes;
    if(q.flags&CB_RENDER_WRITE_COMMIT){s=call(d,(struct cb_io_gpu_request){.operation=CB_GPU_UPLOAD,.resource=t->resource,.context=d->ctx},NULL,0,NULL,0);if(s)return s;t->ready=1;t->writing=0;}
    return CB_OK;
}
cb_status_t cb_gl_surface_patch(struct cb_gl_display *d,uint64_t owner,const void *payload,size_t bytes){
    cb_status_t s=live(d);if(s)return s;
    if(!payload || bytes<sizeof(struct cb_render_patch))return CB_BAD_REQUEST;
    struct cb_render_patch q;__builtin_memcpy(&q,payload,sizeof(q));
    if(q.flags&~(CB_RENDER_WRITE_BEGIN|CB_RENDER_WRITE_COMMIT) || !q.width || !q.height || !q.bytes || q.bytes!=bytes-sizeof(q) || q.bytes>CB_GL_TRANSFER_BYTES)return CB_BAD_REQUEST;
    struct cb_gl_texture *t=surface_find(d,owner,q.surface);if(!t)return CB_BAD_HANDLE;
    if(q.x>t->width || q.y>t->height || q.width>t->width-q.x || q.height>t->height-q.y)return CB_BAD_REQUEST;
    uint64_t row=(uint64_t)q.width*4,total=row*q.height;
    if(q.offset%row || q.bytes%row || q.offset>total || q.bytes>total-q.offset || ((q.flags&CB_RENDER_WRITE_COMMIT) && q.offset+q.bytes!=total))return CB_BAD_REQUEST;
    if(q.flags&CB_RENDER_WRITE_BEGIN){if(!t->ready || t->writing || q.offset)return CB_BAD_REQUEST;}
    else if(t->writing!=2 || q.offset!=t->written || q.x!=t->patch_x || q.y!=t->patch_y || q.width!=t->patch_width || q.height!=t->patch_height)return CB_BAD_REQUEST;
    if(q.flags&CB_RENDER_WRITE_BEGIN){t->ready=0;t->writing=2;t->written=0;t->patch_x=q.x;t->patch_y=q.y;t->patch_width=q.width;t->patch_height=q.height;}
    uint64_t offset=((uint64_t)(q.y+q.offset/row)*t->width+q.x)*4;
    s=call(d,(struct cb_io_gpu_request){.operation=CB_GPU_WRITE,.flags=1,.resource=t->resource,.offset=offset,.width=q.width,.height=q.bytes/row,.bytes=q.bytes},(const unsigned char *)payload+sizeof(q),q.bytes,NULL,0);if(s)return s;
    t->written+=q.bytes;
    if(q.flags&CB_RENDER_WRITE_COMMIT){
        s=call(d,(struct cb_io_gpu_request){.operation=CB_GPU_UPLOAD,.flags=1,.resource=t->resource,.context=d->ctx,.offset=((uint64_t)q.y*t->width+q.x)*4,.width=q.width,.height=q.height},NULL,0,NULL,0);if(s)return s;
        t->writing=0;t->ready=1;
    }
    return CB_OK;
}
cb_status_t cb_gl_surface_compose(struct cb_gl_display *d,uint64_t owner,const void *payload,size_t bytes){
    cb_status_t s=live(d);if(s)return s;
    if(!owner || !payload || bytes<sizeof(struct cb_render_batch))return CB_BAD_REQUEST;
    struct cb_render_batch h;__builtin_memcpy(&h,payload,sizeof(h));
    if(h.version.bytes!=sizeof(h) || h.version.major!=1 || h.version.minor || h.flags&~(CB_RENDER_COMPOSE_BEGIN|CB_RENDER_COMPOSE_END|CB_RENDER_COMPOSE_DAMAGE) || !h.count || (bytes-sizeof(h))%sizeof(struct cb_render_layer) || h.count!=(bytes-sizeof(h))/sizeof(struct cb_render_layer))return CB_BAD_REQUEST;
    if((h.flags&CB_RENDER_COMPOSE_DAMAGE) && !(h.flags&CB_RENDER_COMPOSE_BEGIN))return CB_BAD_REQUEST;
    if(d->composing && d->compose_owner!=owner)return CB_DENIED;
    if(!(h.flags&CB_RENDER_COMPOSE_BEGIN) && !d->composing)return CB_BAD_REQUEST;
    const unsigned char *p=(const unsigned char *)payload+sizeof(h);
    for(unsigned i=0;i<h.count;++i){struct cb_render_layer l;__builtin_memcpy(&l,p+i*sizeof(l),sizeof(l));struct cb_gl_texture *t=surface_find(d,owner,l.surface);if(!t)return CB_BAD_HANDLE;
        if(!t->ready || l.reserved || l.flags&~CB_RENDER_LAYER_ALPHA || !l.width || !l.height || l.x>d->width || l.y>d->height || l.width>d->width-l.x || l.height>d->height-l.y || l.source_x>t->width || l.source_y>t->height || l.width>t->width-l.source_x || l.height>t->height-l.source_y)return CB_BAD_REQUEST;
        if(!i && (h.flags&CB_RENDER_COMPOSE_BEGIN) && (l.flags || (!(h.flags&CB_RENDER_COMPOSE_DAMAGE) && (l.x || l.y || l.width!=d->width || l.height!=d->height))))return CB_BAD_REQUEST;
    }
    if(!d->frame){s=texture(d,d->width,d->height,0,&d->frame);if(s)return s;}
    if(h.flags&CB_RENDER_COMPOSE_BEGIN){d->composing=1;d->compose_owner=owner;}
    uint32_t words[264];struct cb_virgl stream={words,0,264};
    /* Preserve the last complete screen in the back target; all damage is then
     * rebuilt from opaque base + clipped layers. Never blend over stale alpha. */
    if(h.flags&CB_RENDER_COMPOSE_DAMAGE){if(!cb_virgl_blit(&stream,d->frame,2,box(0,0,d->width,d->height),d->screen,2,box(0,0,d->width,d->height),0))return CB_BAD_REQUEST;}
    for(unsigned i=0;i<h.count;++i){struct cb_render_layer l;__builtin_memcpy(&l,p+i*sizeof(l),sizeof(l));struct cb_gl_texture *t=surface_find(d,owner,l.surface);
        if((l.flags&CB_RENDER_LAYER_ALPHA) && !d->alpha_ready){
            if(stream.count){s=submit(d,&stream);if(s)return s;stream.count=0;}
            uint32_t init_words[512];struct cb_virgl init={init_words,0,512};if(!cb_virgl_alpha_init(&init))return CB_BAD_REQUEST;
            s=submit(d,&init);if(s)return s;d->alpha_ready=1;
        }
        uint32_t encoded[128];struct cb_virgl command={encoded,0,128};
        int ok=l.flags&CB_RENDER_LAYER_ALPHA?cb_virgl_alpha(&command,d->frame,d->width,d->height,box(l.x,l.y,l.width,l.height),t->resource,box(l.source_x,l.source_y,l.width,l.height)):cb_virgl_blit(&command,d->frame,2,box(l.x,l.y,l.width,l.height),t->resource,1,box(l.source_x,l.source_y,l.width,l.height),0);
        if(!ok)return CB_BAD_REQUEST;
        if(stream.capacity-stream.count<command.count){s=submit(d,&stream);if(s)return s;stream.count=0;}
        for(size_t j=0;j<command.count;++j)stream.words[stream.count+j]=command.words[j];
        stream.count+=command.count;
    }
    s=submit(d,&stream);if(s)return s;d->dirty=1;
    if(h.flags&CB_RENDER_COMPOSE_END){uint32_t previous=d->screen;d->screen=d->frame;d->frame=previous;d->composing=0;d->compose_owner=0;return cb_gl_display_present(d);}
    return CB_OK;
}
