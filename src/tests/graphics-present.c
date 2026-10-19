/* Exercise the production graphics decoder/presentation with a memory display. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../sdk/service.h"
#include "../interfaces/display.h"
static cb_status_t test_invoke(struct cb_client *,cb_handle_t,struct cb_interface_id,unsigned,const void *,size_t,uint64_t);
#define cb_invoke test_invoke
#define _start graphics_provider_start
#include "../handlers/graphics/graphics.c"
#undef _start
#undef cb_invoke
static uint32_t shown[32*32],backing[32*32];
static unsigned copies,fills,blits,copy_status,fill_status;
static cb_status_t test_invoke(struct cb_client *c,cb_handle_t h,struct cb_interface_id id,unsigned op,const void *p,size_t bytes,uint64_t deadline){
    (void)c;(void)h;(void)id;(void)deadline;
    if(op==CB_DISPLAY_COPY){++copies;if(copy_status){if(copy_status==CB_RESOURCE_FAILURE)shown[0]=0xabcdef;return copy_status;}assert(bytes==sizeof(struct cb_display_copy));struct cb_display_copy box;memcpy(&box,p,sizeof(box));uint32_t saved[32*32];for(unsigned y=0;y<box.height;++y)memcpy(saved+y*box.width,shown+(box.source_y+y)*32+box.source_x,box.width*4);for(unsigned y=0;y<box.height;++y)memcpy(shown+(box.y+y)*32+box.x,saved+y*box.width,box.width*4);return CB_OK;}
    struct cb_rectangle box;memcpy(&box,p,sizeof(box));
    if(op==CB_DISPLAY_FILL){++fills;if(fill_status)return fill_status;assert(bytes==sizeof(box));}
    else {assert(op==CB_DISPLAY_BLIT && bytes==sizeof(box)+(size_t)box.width*box.height*4);++blits;}
    for(unsigned y=0;y<box.height;++y)for(unsigned x=0;x<box.width;++x){uint32_t rgb=box.color;if(op==CB_DISPLAY_BLIT)memcpy(&rgb,(const char *)p+sizeof(box)+((size_t)y*box.width+x)*4,4);shown[(box.y+y)*32+box.x+x]=rgb;}
    return CB_OK;
}
static cb_status_t perform(unsigned op,const void *p,size_t bytes){unsigned char out[128];size_t count=0;return operation(1,op,p,bytes,out,&count);}
static void reset(void){pending_copy.active=0;copies=fills=blits=copy_status=fill_status=0;for(unsigned i=0;i<32*32;++i){backing[i]=0xff000000|i;shown[i]=i;}gfx_init(&screen.raster,backing,32,32);}
static void matching(void){for(unsigned i=0;i<32*32;++i)assert(shown[i]==gfx_rgb(backing[i]));}
int main(void){
    struct cb_graphics_blit scroll={.source_y=2,.width=32,.height=30,.opacity=255};struct cb_rectangle bottom={.y=30,.width=32,.height=2},clear={.width=32,.height=32};
    reset();assert(!perform(CB_GRAPHICS_BLIT,&scroll,sizeof(scroll)) && pending_copy.active && !copies && !blits);
    uint64_t id=0;struct cb_graphics_surface_info info;size_t bytes=0;assert(!operation(1,CB_GRAPHICS_SURFACE_INFO,(const void *)&id,8,&info,&bytes) && info.dirty);
    assert(!perform(CB_GRAPHICS_FILL,&bottom,sizeof(bottom)) && copies==1 && fills==1 && !blits && !pending_copy.active && !screen.raster.dirty);matching();
    /* A full clear supersedes the deferred copy without transferring pixels. */
    reset();assert(!perform(CB_GRAPHICS_BLIT,&scroll,sizeof(scroll)));assert(!perform(CB_GRAPHICS_FILL,&clear,sizeof(clear)) && !copies && fills==1 && !blits);matching();
    /* Unsupported copies retain the pixel-present fallback. Other failures
     * retain backing damage and must not retry a non-idempotent copy. */
    reset();copy_status=CB_UNAVAILABLE;assert(!perform(CB_GRAPHICS_BLIT,&scroll,sizeof(scroll)));assert(!present() && copies==1 && blits && !pending_copy.active);matching();
    reset();for(unsigned y=0;y<32;++y){backing[y*32]=0xff000000;shown[y*32]=0;}copy_status=CB_RESOURCE_FAILURE;assert(!perform(CB_GRAPHICS_BLIT,&scroll,sizeof(scroll)));assert(present()==CB_RESOURCE_FAILURE && !pending_copy.active && screen.raster.dirty);copy_status=0;assert(!present() && copies==1);matching();
    reset();fill_status=CB_RESOURCE_FAILURE;assert(perform(CB_GRAPHICS_FILL,&clear,sizeof(clear))==CB_RESOURCE_FAILURE && screen.raster.dirty);fill_status=0;assert(!present());matching();
    /* Pending damage outside the fill's clip cannot be declared uniform. */
    reset();gfx_damage(&screen.raster,(struct gfx_rect){0,0,32,32});gfx_clip(&screen.raster,(struct gfx_rect){4,4,8,8});assert(!perform(CB_GRAPHICS_FILL,&clear,sizeof(clear)) && blits);matching();
    /* Multiple deferred self-copies and clipping preserve final pixels. */
    reset();assert(!perform(CB_GRAPHICS_BLIT,&scroll,sizeof(scroll)));scroll.x=2;scroll.width=30;scroll.source_y=0;assert(!perform(CB_GRAPHICS_BLIT,&scroll,sizeof(scroll)));assert(!present() && copies==1);matching();
    puts("PASS graphics presentation: deferred region copy, fill, fallback, failed-copy repair, clipping and backing consistency");return 0;
}
