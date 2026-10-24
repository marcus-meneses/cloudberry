#include "../../sdk/service.h"
#include "../../interfaces/graphics.h"
#include "../../interfaces/display.h"
#include "../../interfaces/io.h"
#include "../../interfaces/keyboard.h"
#include "../../interfaces/mouse.h"
#include "../gpu-pair.h"
volatile struct gpu_pair_state gpu_pair_state;
static struct cb_client client;
static cb_handle_t clock_handle,graphics,keyboard,mouse;
static void fail(unsigned line){gpu_pair_state.error=line;__atomic_store_n(&gpu_pair_state.phase,UINT64_MAX,__ATOMIC_RELEASE);cb_exit(line);}
#undef CB_REQUIRE
#define CB_REQUIRE(x) do{if(!(x))fail(__LINE__);}while(0)
static uint64_t now(void){CB_REQUIRE(cb_invoke(&client,clock_handle,(struct cb_interface_id)CB_IO_INTERFACE_ID,CB_IO_NOW_NS,NULL,0,0)==CB_OK);uint64_t n;cb_copy(&n,cb_result(&client),8);return n;}
static void pause_tick(void){uint64_t ready;cb_status_t s=cb_system_wait(NULL,0,now()+1000000,&ready);CB_REQUIRE(s==CB_TIMEOUT || s==CB_OK);}
static void phase(uint64_t n){__atomic_store_n(&gpu_pair_state.phase,n,__ATOMIC_RELEASE);uint64_t deadline=now()+UINT64_C(60000000000);while(__atomic_load_n(&gpu_pair_state.ack,__ATOMIC_ACQUIRE)!=n+1){CB_REQUIRE(now()<deadline);pause_tick();}}
static void draw(unsigned op,const void *p,size_t n){CB_REQUIRE(cb_invoke(&client,graphics,(struct cb_interface_id)CB_GRAPHICS_INTERFACE_ID,op,p,n,0)==CB_OK);}
static uint64_t surface(uint32_t w,uint32_t h,unsigned container){struct cb_graphics_create_ex q={w,h,container?CB_SURFACE_CONTAINER:0,0};draw(CB_GRAPHICS_SURFACE_CREATE,&q,sizeof(q));uint64_t id;cb_copy(&id,cb_result(&client),8);return id;}
static void rectangle(uint64_t id,int x,int y,int w,int h,uint32_t rgb){struct cb_graphics_draw q={.surface=id,.color=0xff000000|rgb,.fill=1,.x={x,w},.y={y,h}};draw(CB_GRAPHICS_RECTANGLE,&q,sizeof(q));}
static void place(uint64_t id,uint64_t parent,int x,int y){struct cb_graphics_place q={id,parent,x,y,CB_SURFACE_VISIBLE,0};draw(CB_GRAPHICS_SURFACE_PLACE,&q,sizeof(q));}
static struct cb_mouse_cursor cursor(void){CB_REQUIRE(cb_invoke(&client,mouse,(struct cb_interface_id)CB_MOUSE_INTERFACE_ID,CB_MOUSE_CURSOR_INFO,NULL,0,0)==CB_OK);struct cb_mouse_cursor c;cb_copy(&c,cb_result(&client),sizeof(c));return c;}
__attribute__((noreturn)) void _start(const struct cb_startup *startup){
    const void *restore_data;size_t restore_bytes;int restoring=startup && cb_startup_find(startup,startup->bytes,"restore",&restore_data,&restore_bytes);gpu_pair_state.magic=GPU_PAIR_MAGIC;
    static const char *names[]={"phase"};(void)cb_publish(&client,GPU_PAIR_NAME,(struct cb_interface_id)GPU_PAIR_ID,names,1);
    clock_handle=cb_acquire(CB_IO_NAME,(struct cb_interface_id)CB_IO_INTERFACE_ID);phase(restoring?9:0);
    graphics=cb_acquire(CB_GRAPHICS_NAME,(struct cb_interface_id)CB_GRAPHICS_INTERFACE_ID);keyboard=cb_acquire(CB_KEYBOARD_NAME,(struct cb_interface_id)CB_KEYBOARD_INTERFACE_ID);mouse=cb_acquire(CB_MOUSE_NAME,(struct cb_interface_id)CB_MOUSE_INTERFACE_ID);
    CB_REQUIRE(cb_invoke(&client,keyboard,(struct cb_interface_id)CB_KEYBOARD_INTERFACE_ID,CB_KEYBOARD_FOCUS_CLAIM,NULL,0,0)==CB_OK);
    struct cb_keyboard_focus focus;cb_copy(&focus,cb_result(&client),sizeof(focus));
    CB_REQUIRE(cb_invoke(&client,mouse,(struct cb_interface_id)CB_MOUSE_INTERFACE_ID,CB_MOUSE_CURSOR_HIDE,NULL,0,0)==CB_OK);
    draw(CB_GRAPHICS_INFO,NULL,0);struct cb_graphics_info info;cb_copy(&info,cb_result(&client),sizeof(info));CB_REQUIRE(info.width>=320 && info.height>=160);gpu_pair_state.width=info.width;gpu_pair_state.height=info.height;
    if(restoring){
        struct cb_rectangle background={0,0,info.width,info.height,0x304050,0};draw(CB_GRAPHICS_FILL,&background,sizeof(background));
        rectangle(0,32,24,80,48,0x55aa11);
        struct {struct cb_graphics_text_batch h;struct cb_glyph g[12];} text={.h={0,16,0}};
        static const char title[]="VGA restored";
        for(size_t i=0;i<sizeof(title)-1;++i)text.g[i]=(struct cb_glyph){.x=16+i*8,.y=96,.codepoint=(unsigned char)title[i],.foreground=0xffffff,.background=0x304050};
        draw(CB_GRAPHICS_TEXT_BATCH,&text,sizeof(text));phase(10);cb_exit(0);
    }
    struct cb_rectangle background={0,0,info.width,info.height,0x102030,0};draw(CB_GRAPHICS_FILL,&background,sizeof(background));
    uint64_t parent=surface(56,56,1),panel=surface(64,64,0),label=surface(192,24,0);
    rectangle(panel,0,0,32,32,0xff0000);rectangle(panel,32,0,32,32,0x00ff00);rectangle(panel,0,32,32,32,0x0000ff);rectangle(panel,32,32,32,32,0xffffff);rectangle(label,0,0,192,24,0x202020);
    struct {struct cb_graphics_text_batch h;struct cb_glyph g[19];} text={.h={label,16,CB_GRAPHICS_DEFER}};
    static const char title[]="Cloudberry OpenGL";
    text.g[0]=(struct cb_glyph){.x=4,.y=1,.codepoint='A',.foreground=0xffffff,.background=0x202020};text.g[1]=(struct cb_glyph){.x=13,.y=1,.codepoint=0xe7,.foreground=0xffaa44,.background=0x202020};
    size_t count=2;for(size_t i=0;i<sizeof(title)-1;++i)text.g[count++]=(struct cb_glyph){.x=32+i*8,.y=1,.codepoint=(unsigned char)title[i],.foreground=0xffffff,.background=0x202020};
    draw(CB_GRAPHICS_TEXT_BATCH,&text,sizeof(text.h)+count*sizeof(text.g[0]));place(parent,0,16,16);place(panel,parent,-8,-8);place(label,0,96,16);uint64_t zero=0;draw(CB_GRAPHICS_PRESENT,&zero,8);phase(1);
#ifdef GPU_PAIR_PROFILE
    for(unsigned i=0;i<32;++i){uint64_t begin=now();rectangle(panel,8,8,8,8,i&1?0xaa2233:0x3377aa);draw(CB_GRAPHICS_PRESENT,&zero,8);uint64_t elapsed=now()-begin;gpu_pair_state.bench_times[i]=elapsed;gpu_pair_state.bench_ns+=elapsed;if(elapsed>gpu_pair_state.bench_max_ns)gpu_pair_state.bench_max_ns=elapsed;++gpu_pair_state.bench_samples;}
    phase(11); /* Observer closes profiling before restoration/readback. */
    rectangle(panel,8,8,8,8,0xff0000);draw(CB_GRAPHICS_PRESENT,&zero,8);
#endif
    place(parent,0,40,24);place(label,0,96,50);draw(CB_GRAPHICS_PRESENT,&zero,8);phase(2);
    CB_REQUIRE(cb_invoke(&client,mouse,(struct cb_interface_id)CB_MOUSE_INTERFACE_ID,CB_MOUSE_CURSOR_SHOW,NULL,0,0)==CB_OK);
    struct cb_mouse_cursor initial=cursor();gpu_pair_state.x=initial.x;gpu_pair_state.y=initial.y;phase(3);
    uint64_t deadline=now()+UINT64_C(20000000000);struct cb_mouse_cursor moved;
    do{moved=cursor();if(moved.x!=initial.x || moved.y!=initial.y)break;CB_REQUIRE(now()<deadline);pause_tick();}while(1);
    CB_REQUIRE(moved.visible && moved.x+16<=info.width && moved.y+24<=info.height);gpu_pair_state.x=moved.x;gpu_pair_state.y=moved.y;phase(4);
    CB_REQUIRE(cb_invoke(&client,mouse,(struct cb_interface_id)CB_MOUSE_INTERFACE_ID,CB_MOUSE_CURSOR_HIDE,NULL,0,0)==CB_OK);phase(5);phase(6);
    struct cb_keyboard_read read={.version={sizeof(read),1,0},.provider_epoch=focus.provider_epoch,.after_sequence=focus.next_sequence,.max_events=16};deadline=now()+UINT64_C(20000000000);
    while(!gpu_pair_state.key){cb_status_t status=cb_invoke(&client,keyboard,(struct cb_interface_id)CB_KEYBOARD_INTERFACE_ID,CB_KEYBOARD_READ,&read,sizeof(read),deadline);CB_REQUIRE(status==CB_OK || status==CB_INPUT_RESYNC);struct cb_keyboard_batch batch;cb_copy(&batch,cb_result(&client),sizeof(batch));read.provider_epoch=batch.provider_epoch;read.after_sequence=batch.next_sequence;for(unsigned i=0;i<batch.count;++i){struct cb_keyboard_event e;cb_copy(&e,(unsigned char *)cb_result(&client)+sizeof(batch)+i*sizeof(e),sizeof(e));if(e.kind==CB_KEY_TEXT && e.codepoint=='a')gpu_pair_state.key=e.codepoint;}}
    phase(7);
    /* Deliberately leave surfaces, focus and hidden cursor owned. Exit cleanup
     * must remove app layers and restore the mouse without app cooperation. */
    cb_exit(0);
}
