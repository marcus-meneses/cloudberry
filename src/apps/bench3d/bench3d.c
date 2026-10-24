/* CPU-projected geometry, ordinary exchangeable /@graphics backend. */
#include "../../sdk/application.h"
#include "../../sdk/service.h"
#include "../../sdk/stream.h"
#include "../../interfaces/graphics.h"
#include "../../interfaces/keyboard.h"
#include "../../interfaces/mouse.h"
#include "../../interfaces/io.h"
#include "model.h"
static struct cb_client client,keys;
static cb_handle_t graphics,clock_handle,keyboard,mouse;
static uint64_t scene,hud,footer;static unsigned focused,hidden,cubes=4,wire,paused;
static struct cb_keyboard_read_focus keyread={.base={.version={sizeof(struct cb_keyboard_read_focus),1,1},.max_events=32}};
static struct bench_face faces[6*BENCH3D_MAX_CUBES];
static struct cb_graphics_info display;
static struct cb_graphics_font font;
static uint32_t hud_height,footer_height;
static uint64_t frames,window_frames,window_start,window_ns,last_ns,min_ns=UINT64_MAX,max_ns,average_ns,fps10,build_ns,draw_ns,present_ns;
static unsigned visible_faces,calls,last_calls,paint_hud=1;
static void close_app(void){
    if(keys.pending.serial){struct cb_abandon_result a;(void)cb_abandon(&keys,&a);}
    uint64_t surfaces[]={scene,hud,footer};for(unsigned i=0;i<3;++i)if(surfaces[i])(void)cb_invoke(&client,graphics,(struct cb_interface_id)CB_GRAPHICS_INTERFACE_ID,CB_GRAPHICS_SURFACE_RELEASE,&surfaces[i],8,0);
    if(graphics){uint64_t zero=0;(void)cb_invoke(&client,graphics,(struct cb_interface_id)CB_GRAPHICS_INTERFACE_ID,CB_GRAPHICS_PRESENT,&zero,8,0);}
    if(hidden)(void)cb_invoke(&client,mouse,(struct cb_interface_id)CB_MOUSE_INTERFACE_ID,CB_MOUSE_CURSOR_SHOW,NULL,0,0);
    if(focused)(void)cb_invoke(&client,keyboard,(struct cb_interface_id)CB_KEYBOARD_INTERFACE_ID,CB_KEYBOARD_FOCUS_RELEASE,NULL,0,0);
    cb_handle_t handles[]={mouse,keyboard,clock_handle,graphics};for(unsigned i=0;i<4;++i)if(handles[i])(void)cb_system_release(handles[i]);
}
static __attribute__((noreturn)) void failure(unsigned line){
    close_app();char message[80]="bench3d: operation failed at line ";size_t n=cb_length(message);char digits[12];unsigned used=0;do{digits[used++]='0'+line%10;line/=10;}while(line);while(used)message[n++]=digits[--used];message[n++]='\n';cb_handle_t out=cb_standard(2);if(out)(void)cb_write_all(&client,out,message,n);cb_exit(CB_RESOURCE_FAILURE);
}
#undef CB_REQUIRE
#define CB_REQUIRE(x) do{if(!(x))failure(__LINE__);}while(0)
static cb_handle_t acquire(const char *name,struct cb_interface_id id){cb_handle_t h=0;CB_REQUIRE(cb_system_get_handle(name,cb_length(name),&id,1,0,&h)==CB_OK);return h;}
static uint64_t now(void){CB_REQUIRE(cb_invoke(&client,clock_handle,(struct cb_interface_id)CB_IO_INTERFACE_ID,CB_IO_NOW_NS,NULL,0,0)==CB_OK);uint64_t t;cb_copy(&t,cb_result(&client),8);return t;}
static void gfx(unsigned op,const void *p,size_t n){++calls;CB_REQUIRE(cb_invoke(&client,graphics,(struct cb_interface_id)CB_GRAPHICS_INTERFACE_ID,op,p,n,0)==CB_OK);}
static uint64_t surface(unsigned width,unsigned height,int y){struct cb_graphics_create c={width,height};gfx(CB_GRAPHICS_SURFACE_CREATE,&c,sizeof(c));uint64_t id;cb_copy(&id,cb_result(&client),8);struct cb_graphics_place p={id,0,0,y,CB_SURFACE_VISIBLE,0};gfx(CB_GRAPHICS_SURFACE_PLACE,&p,sizeof(p));return id;}
static void fill(uint64_t id,unsigned height,uint32_t rgb){struct cb_graphics_draw d={.surface=id,.color=0xff000000|rgb,.fill=1,.x={0,(int32_t)display.width},.y={0,(int32_t)height}};gfx(CB_GRAPHICS_RECTANGLE,&d,sizeof(d));}
struct line {char text[180];size_t n;};
static void word(struct line *l,const char *s){while(*s && l->n<sizeof(l->text))l->text[l->n++]=*s++;}
static void number(struct line *l,uint64_t v){char digits[20];unsigned n=0;do{digits[n++]='0'+v%10;v/=10;}while(v);while(n && l->n<sizeof(l->text))l->text[l->n++]=digits[--n];}
static void decimal(struct line *l,uint64_t tenths){number(l,tenths/10);word(l,".");number(l,tenths%10);}
static void label(uint64_t id,unsigned row,const struct line *line,uint32_t background){
    struct {struct cb_graphics_text_batch h;struct cb_glyph g[180];} packet={.h={id,font.pixel_size,CB_GRAPHICS_DEFER}};
    size_t count=line->n,limit=display.width>24?(display.width-24)/font.cell_width:0;if(count>limit)count=limit;if(!count)return;
    for(size_t i=0;i<count;++i){packet.g[i]=(struct cb_glyph){.x=font.cell_width+i*font.cell_width,.y=row*font.cell_height,.codepoint=(unsigned char)line->text[i],.foreground=0xe8edf3,.background=background};}
    gfx(CB_GRAPHICS_TEXT_BATCH,&packet,sizeof(packet.h)+count*sizeof(packet.g[0]));
}
static void statistics(void){
    fill(hud,hud_height,0x192432);struct line l={0};word(&l,"Cloudberry 3D benchmark | CPU-projected cubes | ");word(&l,wire?"wireframe":"solid");if(paused)word(&l," | PAUSED");label(hud,0,&l,0x192432);
    l=(struct line){0};word(&l,"FPS ");decimal(&l,fps10);word(&l," | Avg ");decimal(&l,average_ns/100000);word(&l," ms | Last ");decimal(&l,last_ns/100000);word(&l," | Min ");decimal(&l,min_ns==UINT64_MAX?0:min_ns/100000);word(&l," | Max ");decimal(&l,max_ns/100000);label(hud,1,&l,0x192432);
    l=(struct line){0};word(&l,"Cubes ");number(&l,cubes);word(&l," | Vertices ");number(&l,cubes*8);word(&l," | Faces ");number(&l,visible_faces);word(&l," | Graphics calls/frame ");number(&l,last_calls);word(&l," | Frames ");number(&l,frames);label(hud,2,&l,0x192432);
    l=(struct line){0};word(&l,"Build ");decimal(&l,build_ns/100000);word(&l," ms | Draw ");decimal(&l,draw_ns/100000);word(&l," ms | Present ");decimal(&l,present_ns/100000);word(&l," ms | ");number(&l,display.width);word(&l,"x");number(&l,display.height);label(hud,3,&l,0x192432);paint_hud=0;
}
static void arm_keys(void){if(!keys.pending.serial)CB_REQUIRE(cb_submit(&keys,keyboard,(struct cb_interface_id)CB_KEYBOARD_INTERFACE_ID,CB_KEYBOARD_READ,&keyread,sizeof(keyread))==CB_OK);}
static int input(void){
    arm_keys();cb_status_t status=cb_collect(&keys);if(status==CB_WOULD_BLOCK && !keys.collected)return 0;CB_REQUIRE(status==CB_OK || status==CB_INPUT_RESYNC);
    struct cb_keyboard_batch_focus b;CB_REQUIRE(keys.bytes>=sizeof(struct cb_reply_header)+sizeof(b));cb_copy(&b,cb_result(&keys),sizeof(b));CB_REQUIRE(b.base.version.bytes==sizeof(b) && b.base.version.major==1 && b.base.version.minor==1 && b.base.count<=32 && sizeof(b)+(uint64_t)b.base.count*sizeof(struct cb_keyboard_event)<=keys.bytes-sizeof(struct cb_reply_header));
    keyread.base.provider_epoch=b.base.provider_epoch;keyread.base.after_sequence=b.base.next_sequence;keyread.focus_generation=b.focus_generation;
    if(status==CB_OK)for(unsigned i=0;i<b.base.count;++i){struct cb_keyboard_event e;cb_copy(&e,(unsigned char *)cb_result(&keys)+sizeof(b)+i*sizeof(e),sizeof(e));CB_REQUIRE(cb_keyboard_event_valid(&e));
        if(e.kind==CB_KEY_DOWN && e.usage_page==7){if(e.usage==41)return 1;if(e.usage==44){paused=!paused;paint_hud=1;window_start=0;}}
        if(e.kind!=CB_KEY_TEXT)continue;
        if(e.codepoint=='+' || e.codepoint=='='){if(cubes<BENCH3D_MAX_CUBES)cubes=cubes>BENCH3D_MAX_CUBES/2?BENCH3D_MAX_CUBES:cubes*2;paint_hud=1;window_start=0;}
        if(e.codepoint=='-'){if(cubes>1)cubes/=2;paint_hud=1;window_start=0;}
        if(e.codepoint=='w' || e.codepoint=='W'){wire=!wire;paint_hud=1;window_start=0;}
        if(e.codepoint=='r' || e.codepoint=='R'){window_start=0;min_ns=UINT64_MAX;max_ns=0;paint_hud=1;}
    }
    arm_keys();return 0;
}
static unsigned integer(const char *s){CB_REQUIRE(s && *s);unsigned n=0;while(*s){CB_REQUIRE(*s>='0' && *s<='9' && n<=(UINT32_MAX-(unsigned)(*s-'0'))/10);n=n*10+*s++-'0';}CB_REQUIRE(n);return n;}
__attribute__((noreturn)) void _start(const struct cb_startup *packet){
    struct cb_application_start start;CB_REQUIRE(cb_application_start(packet,&start));uint64_t limit=0;
    for(uint64_t i=1;i<start.header.argc;++i){const char *arg=cb_application_argument(&start,i);if(cb_length(arg)==8 && !__builtin_memcmp(arg,"--frames",8))limit=integer(cb_application_argument(&start,++i));else if(cb_length(arg)==7 && !__builtin_memcmp(arg,"--cubes",7))cubes=integer(cb_application_argument(&start,++i));else cb_exit(CB_BAD_REQUEST);}CB_REQUIRE(cubes<=BENCH3D_MAX_CUBES);
    graphics=acquire(CB_GRAPHICS_NAME,(struct cb_interface_id)CB_GRAPHICS_INTERFACE_ID);clock_handle=acquire(CB_IO_NAME,(struct cb_interface_id)CB_IO_INTERFACE_ID);keyboard=acquire(CB_KEYBOARD_NAME,(struct cb_interface_id)CB_KEYBOARD_INTERFACE_ID);
    gfx(CB_GRAPHICS_INFO,NULL,0);cb_copy(&display,cb_result(&client),sizeof(display));gfx(CB_GRAPHICS_FONT_INFO,NULL,0);cb_copy(&font,cb_result(&client),sizeof(font));CB_REQUIRE(display.width>=320 && display.height>=240 && font.cell_width && font.cell_height && font.cell_height<display.height/8);
    hud_height=font.cell_height*4+8;footer_height=font.cell_height+8;scene=surface(display.width,display.height-hud_height-footer_height,hud_height);hud=surface(display.width,hud_height,0);footer=surface(display.width,footer_height,display.height-footer_height);
    struct cb_keyboard_focus focus;CB_REQUIRE(cb_invoke(&client,keyboard,(struct cb_interface_id)CB_KEYBOARD_INTERFACE_ID,CB_KEYBOARD_FOCUS_CLAIM,NULL,0,0)==CB_OK);cb_copy(&focus,cb_result(&client),sizeof(focus));focused=1;keyread.base.provider_epoch=focus.provider_epoch;keyread.base.after_sequence=focus.next_sequence;keyread.focus_generation=focus.generation;
    struct cb_interface_id mid=CB_MOUSE_INTERFACE_ID;cb_status_t ms=cb_system_get_handle(CB_MOUSE_NAME,cb_length(CB_MOUSE_NAME),&mid,1,0,&mouse);CB_REQUIRE(ms==CB_OK || ms==CB_NOT_FOUND || ms==CB_UNAVAILABLE || ms==CB_DENIED);if(!ms){CB_REQUIRE(cb_invoke(&client,mouse,mid,CB_MOUSE_CURSOR_HIDE,NULL,0,0)==CB_OK);hidden=1;}
    fill(footer,footer_height,0x192432);struct line help={0};word(&help,"Esc quit | +/- load | W wireframe | Space pause | R reset stats");label(footer,0,&help,0x192432);arm_keys();uint64_t epoch=now(),angle=0;
    while(!input()){
        if(paused){if(paint_hud){statistics();uint64_t zero=0;gfx(CB_GRAPHICS_PRESENT,&zero,8);}struct cb_wait_item item={.kind=CB_WAIT_REQUEST,.request=keys.pending};uint64_t ready;CB_REQUIRE(cb_system_wait(&item,1,0,&ready)==CB_OK);continue;}
        uint64_t begin=now();if(!window_start){window_start=begin;window_frames=window_ns=0;}
        angle=(begin-epoch)/UINT64_C(8000000);calls=0;visible_faces=bench_scene(faces,cubes,(unsigned)angle,display.width,display.height-hud_height-footer_height);uint64_t built=now();CB_REQUIRE(built>=begin);
        fill(scene,display.height-hud_height-footer_height,0x0b111c);
        for(unsigned f=0;f<visible_faces;++f){struct {struct cb_graphics_draw draw;struct cb_graphics_segment path[5];} face={.draw={.surface=scene,.color=faces[f].color,.fill=!wire}};
            for(unsigned i=0;i<4;++i){face.path[i]=(struct cb_graphics_segment){.command=i?CB_PATH_LINE:CB_PATH_MOVE,.x={faces[f].p[i].x},.y={faces[f].p[i].y}};}
            face.path[4].command=CB_PATH_CLOSE;gfx(CB_GRAPHICS_PATH,&face,sizeof(face));}
        uint64_t drawn=now();CB_REQUIRE(drawn>=built);if(paint_hud || drawn-window_start>=UINT64_C(1000000000))statistics();
        uint64_t present_begin=now(),zero=0;gfx(CB_GRAPHICS_PRESENT,&zero,8);uint64_t end=now();CB_REQUIRE(end>=present_begin && present_begin>=drawn);build_ns=built-begin;draw_ns=drawn-built;present_ns=end-present_begin;last_calls=calls;last_ns=end-begin;++frames;++window_frames;window_ns+=last_ns;if(last_ns<min_ns)min_ns=last_ns;if(last_ns>max_ns)max_ns=last_ns;
        if(end-window_start>=UINT64_C(1000000000)){fps10=window_frames*UINT64_C(10000000000)/(end-window_start);average_ns=window_ns/window_frames;window_start=end;window_frames=window_ns=0;paint_hud=1;}
        if(limit && frames>=limit)break;
    }
    close_app();struct line summary={0};word(&summary,"bench3d: frames=");number(&summary,frames);word(&summary," last_ms=");decimal(&summary,last_ns/100000);word(&summary,"\n");cb_handle_t out=cb_standard(1);if(out)(void)cb_write_all(&client,out,summary.text,summary.n);cb_exit(0);
}
