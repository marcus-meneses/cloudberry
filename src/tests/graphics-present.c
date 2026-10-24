/* Exercise the production graphics decoder/presentation with a memory display. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "../sdk/service.h"
#include "../interfaces/display.h"
static cb_status_t test_invoke(struct cb_client *,cb_handle_t,struct cb_interface_id,unsigned,const void *,size_t,uint64_t);
static unsigned pixel_checks;
#define CB_SCENE_PIXEL_CHECK() (++pixel_checks)
#define cb_invoke test_invoke
#define _start graphics_provider_start
#include "../handlers/graphics-vga/graphics-vga.c"
#undef _start
#undef cb_invoke
static uint32_t shown[32*32],backing[32*32];
static unsigned copies,fills,blits,copy_status,fill_status,blit_status;
static cb_status_t test_invoke(struct cb_client *c,cb_handle_t h,struct cb_interface_id id,unsigned op,const void *p,size_t bytes,uint64_t deadline){
    (void)c;(void)h;(void)deadline;
    const struct cb_interface_id memory_id=CB_MEMORY_INTERFACE_ID;if(!memcmp(&id,&memory_id,sizeof(id))){struct cb_memory_request r;memcpy(&r,p,sizeof(r));if(op==CB_MEMORY_FREE){free((void *)(uintptr_t)r.address);return CB_OK;}assert(op==CB_MEMORY_ALLOCATE);struct cb_memory_result a={.address=(uintptr_t)calloc(1,r.bytes)};memcpy(cb_result(c),&a,sizeof(a));return CB_OK;}
    const struct cb_interface_id io_id=CB_IO_INTERFACE_ID;if(!memcmp(&id,&io_id,sizeof(id)))return CB_OK;
    if(op==CB_DISPLAY_COPY){++copies;if(copy_status){if(copy_status==CB_RESOURCE_FAILURE)shown[0]=0xabcdef;return copy_status;}assert(bytes==sizeof(struct cb_display_copy));struct cb_display_copy box;memcpy(&box,p,sizeof(box));uint32_t saved[32*32];for(unsigned y=0;y<box.height;++y)memcpy(saved+y*box.width,shown+(box.source_y+y)*32+box.source_x,box.width*4);for(unsigned y=0;y<box.height;++y)memcpy(shown+(box.y+y)*32+box.x,saved+y*box.width,box.width*4);return CB_OK;}
    struct cb_rectangle box;memcpy(&box,p,sizeof(box));
    if(op==CB_DISPLAY_FILL){++fills;if(fill_status)return fill_status;assert(bytes==sizeof(box));}
    else {assert(op==CB_DISPLAY_BLIT && bytes==sizeof(box)+(size_t)box.width*box.height*4);++blits;if(blit_status)return blit_status;}
    for(unsigned y=0;y<box.height;++y)for(unsigned x=0;x<box.width;++x){uint32_t rgb=box.color;if(op==CB_DISPLAY_BLIT)memcpy(&rgb,(const char *)p+sizeof(box)+((size_t)y*box.width+x)*4,4);shown[(box.y+y)*32+box.x+x]=rgb;}
    return CB_OK;
}
static cb_status_t perform(unsigned op,const void *p,size_t bytes){static struct screen_context context;context.owner=1;context.clip=screen.raster.clip;context.mode=screen.raster.mode;memcpy(context.matrix,screen.raster.matrix,sizeof(context.matrix));contexts=&context;unsigned char out[128];size_t count=0;return operation(1,op,p,bytes,out,&count);}
static void reset(void){while(surfaces)destroy(surfaces);if(scene_cache.pixels){free(scene_cache.pixels);free(scene_work.pixels);}memset(&scene_cache,0,sizeof(scene_cache));memset(&scene_work,0,sizeof(scene_work));scene_roots=NULL;scene_count=scene_valid=scene_retry=scene_copy.active=0;scene_generation=0;scene_owner=1;pending_copy.active=0;sparse_count=sparse_active=overlay_damage_count=0;overlays=NULL;copies=fills=blits=copy_status=fill_status=blit_status=0;for(unsigned i=0;i<32*32;++i){backing[i]=0xff000000|i;shown[i]=i;}gfx_init(&screen.raster,backing,32,32);}
static void matching(void){for(unsigned i=0;i<32*32;++i)assert(shown[i]==gfx_rgb(backing[i]));}
static uint64_t create(unsigned w,unsigned h,unsigned container,uint64_t owner){struct cb_graphics_create_ex q={w,h,container,0};uint64_t id;size_t bytes=0;assert(!operation(owner,CB_GRAPHICS_SURFACE_CREATE,(void *)&q,sizeof(q),&id,&bytes) && bytes==8);return id;}
static void fill_surface(uint64_t id,uint32_t color){struct surface *s=find(id,1);struct cb_graphics_draw q={.surface=id,.fill=1,.color=color,.x={0,(int32_t)s->raster.width},.y={0,(int32_t)s->raster.height}};assert(!perform(CB_GRAPHICS_RECTANGLE,&q,sizeof(q)));}
static void scene_tests(void){
    reset();uint64_t parent=create(32,32,1,1),lines[4];struct cb_graphics_place q[5]={{.surface=parent,.flags=1}};
    for(unsigned i=0;i<4;i++){lines[i]=create(32,8,0,1);fill_surface(lines[i],0xff102030+i);q[i+1]=(struct cb_graphics_place){.surface=lines[i],.parent=parent,.y=i*8,.flags=1};}
    assert(!perform(CB_GRAPHICS_SURFACE_PLACE,q,sizeof(q)));assert(!present());assert(shown[0]==0x102030 && shown[31*32]==0x102033);
    uint64_t generations[4];for(unsigned i=0;i<4;i++)generations[i]=find(lines[i],1)->raster.generation;
    copies=fills=blits=pixel_checks=0;struct cb_graphics_scroll scroll={parent,0,-8};assert(!perform(CB_GRAPHICS_SURFACE_SCROLL,&scroll,sizeof(scroll)) && scene_copy.active);
    struct cb_graphics_place recycle={.surface=lines[0],.parent=parent,.y=24,.flags=1};assert(!perform(CB_GRAPHICS_SURFACE_PLACE,&recycle,sizeof(recycle)));fill_surface(lines[0],0xff000000);assert(!present() && copies==1 && fills==1 && !blits && pixel_checks);assert(shown[0]==0x102031 && shown[24*32]==0);
    for(unsigned i=1;i<4;i++)assert(find(lines[i],1)->raster.generation==generations[i]);
    /* Movement reuses bitmap generations and clips descendants to parents. */
    recycle=(struct cb_graphics_place){.surface=parent,.x=10,.y=5,.flags=1};pixel_checks=0;assert(!perform(CB_GRAPHICS_SURFACE_PLACE,&recycle,sizeof(recycle)));assert(!present());assert(pixel_checks && shown[5*32+10]==0x102031 && shown[5*32+9]==gfx_rgb(backing[5*32+9]));
    struct cb_graphics_place cycle[2]={{.surface=parent,.parent=lines[1],.flags=1},{.surface=lines[1],.parent=parent,.flags=1}};assert(perform(CB_GRAPHICS_SURFACE_PLACE,cycle,sizeof(cycle))==CB_BAD_REQUEST && !find(parent,1)->parent);
    struct cb_graphics_place foreign={.surface=lines[1],.parent=create(4,4,1,2),.flags=1};assert(perform(CB_GRAPHICS_SURFACE_PLACE,&foreign,sizeof(foreign))==CB_DENIED);
    /* Cache comparison must not suppress retry after a failed presentation:
     * the RAM image can already contain the new value while the display does not. */
    fill_surface(lines[1],0xff112233);fill_status=CB_RESOURCE_FAILURE;assert(present()==CB_RESOURCE_FAILURE && scene_retry);fill_status=0;pixel_checks=0;assert(!present() && !scene_retry && !pixel_checks && shown[5*32+10]==0x112233);
    /* An authenticated foreground switch hides the previous owner's tree. */
    scene_select(2);assert(!present());matching();scene_select(1);assert(!present());assert(shown[5*32+10]==0x112233);
    /* Releasing a parent safely detaches live children, which can be reused. */
    assert(!perform(CB_GRAPHICS_SURFACE_RELEASE,&parent,8));assert(!find(lines[1],1)->attached && find(lines[1],1));assert(!present());matching();
    /* Reparent a previously nested parent in a transaction whose reverse
     * application order would temporarily create a cycle. */
    uint64_t a=create(8,8,1,1),b=create(8,8,1,1);struct cb_graphics_place before[2]={{.surface=a,.flags=1},{.surface=b,.parent=a,.flags=1}};assert(!perform(CB_GRAPHICS_SURFACE_PLACE,before,sizeof(before)));struct cb_graphics_place after[2]={{.surface=a,.parent=b,.flags=1},{.surface=b,.flags=1}};assert(!perform(CB_GRAPHICS_SURFACE_PLACE,after,sizeof(after)));assert(find(a,1)->parent==find(b,1) && !find(b,1)->parent);
    /* A relocated solid layer can compose to identical pixels. Compare the
     * damaged bounds and avoid sending their unchanged contents to display. */
    reset();uint64_t solid=create(32,48,0,1);fill_surface(solid,0xff102030);
    struct cb_graphics_place move={.surface=solid,.y=-8,.flags=1};
    assert(!perform(CB_GRAPHICS_SURFACE_PLACE,&move,sizeof(move)) && !present());
    copies=fills=blits=pixel_checks=0;move.y=-7;
    assert(!perform(CB_GRAPHICS_SURFACE_PLACE,&move,sizeof(move)) && !present());
    assert(pixel_checks && !copies && !fills && !blits);
    for(unsigned i=0;i<32*32;++i)assert(shown[i]==0x102030);
    /* Actual geometry changes still update the old and new exposed regions. */
    pixel_checks=0;move.y=1;
    assert(!perform(CB_GRAPHICS_SURFACE_PLACE,&move,sizeof(move)) && !present());
    assert(pixel_checks && shown[0]==gfx_rgb(backing[0]) && shown[32]==0x102030);
    reset();
}
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
    /* Moving and hiding a layer restores current backing, including damage
     * rendered beneath it while visible; no saved-background pixel staleness. */
    reset();uint32_t arrow[4]={0xffffffff,0,0xff101010,0xffffffff};struct overlay layer={.owner=7,.width=2,.height=2,.visible=1,.pixels=arrow,.x=3,.y=3};overlays=&layer;overlay_dirty(layer_rect(&layer));assert(!present_overlay());assert(shown[3*32+3]==0xffffff && backing[3*32+3]==(0xff000000u|99));
    backing[3*32+3]=0xff123456;gfx_damage(&screen.raster,(struct gfx_rect){3,3,1,1});assert(!present());assert(shown[3*32+3]==0xffffff);
    struct cb_overlay_move move={20,20};unsigned char output[128];size_t output_bytes=0;assert(!overlay_call(7,CB_OVERLAY_MOVE,&move,sizeof(move),output,&output_bytes));assert(shown[3*32+3]==0x123456 && shown[20*32+20]==0xffffff);
    blit_status=CB_RESOURCE_FAILURE;move=(struct cb_overlay_move){14,14};assert(overlay_call(7,CB_OVERLAY_MOVE,&move,sizeof(move),output,&output_bytes)==CB_RESOURCE_FAILURE);blit_status=0;move=(struct cb_overlay_move){10,10};assert(!overlay_call(7,CB_OVERLAY_MOVE,&move,sizeof(move),output,&output_bytes));for(unsigned y=0;y<32;y++)for(unsigned x=0;x<32;x++)assert(shown[y*32+x]==overlay_rgb(x,y,gfx_rgb(backing[y*32+x])));
    assert(!overlay_call(7,CB_OVERLAY_HIDE,NULL,0,output,&output_bytes));matching();
    layer.visible=1;layer.x=3;layer.y=4;overlay_dirty(layer_rect(&layer));assert(!present_overlay());scroll=(struct cb_graphics_blit){.source_y=2,.width=32,.height=30,.opacity=255};assert(!perform(CB_GRAPHICS_BLIT,&scroll,sizeof(scroll)));assert(!present() && copies==1);for(unsigned y=0;y<32;y++)for(unsigned x=0;x<32;x++)assert(shown[y*32+x]==overlay_rgb(x,y,gfx_rgb(backing[y*32+x])));overlays=NULL;
    /* Distant edits remain two transfers rather than a full bounding box. */
    reset();sparse_active=1;backing[0]=0xff123456;backing[31*32+31]=0xff654321;gfx_damage(&screen.raster,(struct gfx_rect){0,0,32,32});sparse_add((struct gfx_rect){0,0,1,1});sparse_add((struct gfx_rect){31,31,1,1});assert(!present() && fills==2 && !blits);matching();
    scene_tests();
    puts("PASS retained hierarchy/overlay/sparse graphics presentation: deferred region copy, fill, fallback, failed-copy repair, clipping and backing consistency");return 0;
}
