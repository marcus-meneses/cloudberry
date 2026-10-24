/* Device-independent cursor policy lives here. The generic graphics overlay
 * owns compositing; display/backing pixels are never saved/restored by mouse. */
static cb_handle_t overlay;
static struct cb_mouse_cursor cursor;
static uint64_t cursor_hidden_owner;static unsigned cursor_installed;
#define CURSOR_WATCH UINT64_C(0x435552534f52)
static cb_status_t cursor_call(unsigned op,const void *p,size_t bytes){return cb_invoke(&client,overlay,(struct cb_interface_id)CB_GRAPHICS_OVERLAY_INTERFACE_ID,op,p,bytes,0);}
static void cursor_init(void){
    overlay=cb_acquire(CB_GRAPHICS_OVERLAY_NAME,(struct cb_interface_id)CB_GRAPHICS_OVERLAY_INTERFACE_ID);CB_REQUIRE(cursor_call(CB_OVERLAY_INFO,NULL,0)==CB_OK);struct cb_display_info info;cb_copy(&info,cb_result(&client),sizeof(info));CB_REQUIRE(info.width && info.height);cursor=(struct cb_mouse_cursor){info.width>24?info.width-24:0,info.height>24?info.height-24:0,info.width,info.height,0,0};
}
static void cursor_shape(void){
    static const char *const arrow[]={"X...............","XX..............","XOX.............","XOOX............","XOOOX...........","XOOOOX..........","XOOOOOX.........","XOOOOOOX........","XOOOOOOOX.......","XOOOOOOOOX......","XOOOOOOOOOX.....","XOOOOOOOOOOX....","XOOOOOOOOOOOX...","XOOOOOOXXXXXX...","XOOOXOOX........","XOOX.XOOX.......","XOX..XOOX.......","XX....XOOX......","X.....XOOX......",".......XOOX.....",".......XOOX.....","........XX......","................","................"};
    unsigned char data[sizeof(struct cb_overlay_set)+16*24*4];struct cb_overlay_set q={(int32_t)cursor.x,(int32_t)cursor.y,16,24};cb_copy(data,&q,sizeof(q));for(unsigned y=0;y<24;++y)for(unsigned x=0;x<16;++x){uint32_t color=arrow[y][x]=='X'?0xff101010:arrow[y][x]=='O'?0xfffafafa:0;cb_copy(data+sizeof(q)+(y*16+x)*4,&color,4);}CB_REQUIRE(cursor_call(CB_OVERLAY_SET,data,sizeof(data))==CB_OK);cursor.visible=cursor_installed=1;
}
static cb_status_t cursor_watch(uint64_t pid,unsigned enabled){struct cb_io_owner_watch watch={pid,CURSOR_WATCH,enabled};return invoke(CB_IO_OWNER_WATCH,&watch,sizeof(watch));}
static void cursor_restore(void){
    if(cursor_hidden_owner)return;
    int present=0;for(struct mouse *m=devices;m;m=m->next)if(!m->rejected)present=1;
    if(!present)return;
    if(!cursor_installed){cursor_shape();return;}
    struct cb_overlay_move p={(int32_t)cursor.x,(int32_t)cursor.y};CB_REQUIRE(cursor_call(CB_OVERLAY_MOVE,&p,sizeof(p))==CB_OK);cursor.visible=1;
}
static void cursor_end_hide(void){uint64_t owner=cursor_hidden_owner;if(owner){cursor_hidden_owner=0;CB_REQUIRE(cursor_watch(owner,0)==CB_OK);}cursor_restore();}
static cb_status_t cursor_control(uint64_t pid,unsigned hide){
    if(!hide){if(cursor_hidden_owner && cursor_hidden_owner!=pid)return CB_DENIED;if(cursor_hidden_owner)cursor_end_hide();return CB_OK;}
    cb_status_t status=invoke(CB_IO_INPUT_FOCUS,&pid,8);if(status)return status;struct cb_io_input_focus focused;cb_copy(&focused,cb_result(&client),sizeof(focused));if(focused.process!=pid || focused.session!=focused.caller_session)return CB_DENIED;
    if(cursor_hidden_owner==pid)return CB_OK;
    if(cursor_hidden_owner)cursor_end_hide();
    status=cursor_watch(pid,1);if(status)return status;cursor_hidden_owner=pid;
    if(cursor.visible){CB_REQUIRE(cursor_call(CB_OVERLAY_HIDE,NULL,0)==CB_OK);cursor.visible=0;}return CB_OK;
}
static void cursor_check_focus(void){if(!cursor_hidden_owner)return;uint64_t owner=cursor_hidden_owner;cb_status_t status=invoke(CB_IO_INPUT_FOCUS,&owner,8);struct cb_io_input_focus focus={0};if(!status)cb_copy(&focus,cb_result(&client),sizeof(focus));if(status==CB_REVOKED || (!status && focus.process!=owner))cursor_end_hide();}
static uint32_t cursor_axis(uint32_t old,int64_t delta,uint32_t length){if(delta<0){uint64_t magnitude=(uint64_t)(-(delta+1))+1;return magnitude>old?0:old-(uint32_t)magnitude;}uint64_t available=length-1-old;return (uint64_t)delta>available?length-1:old+(uint32_t)delta;}
static void cursor_update(struct mouse *device,unsigned kind,const struct mouse_hid_input *value){
    cursor_check_focus();
    if(kind==CB_MOUSE_ADDED && !cursor.visible){cursor_restore();return;}
    if(kind==CB_MOUSE_REMOVED){int another=0;for(struct mouse *m=devices;m;m=m->next)if(m!=device && !m->rejected)another=1;if(!another && cursor.visible){CB_REQUIRE(cursor_call(CB_OVERLAY_HIDE,NULL,0)==CB_OK);cursor.visible=0;}return;}
    if(kind!=CB_MOUSE_INPUT || !value)return;
    uint32_t x=cursor.x,y=cursor.y;
    if(value->flags&CB_MOUSE_ABSOLUTE){int64_t ax=value->x,ay=value->y;if(ax<0)ax=0;if(ay<0)ay=0;if(ax>65535)ax=65535;if(ay>65535)ay=65535;cursor.x=(uint64_t)ax*(cursor.width-1)/65535;cursor.y=(uint64_t)ay*(cursor.height-1)/65535;}
    else {cursor.x=cursor_axis(cursor.x,value->dx,cursor.width);cursor.y=cursor_axis(cursor.y,value->dy,cursor.height);}
    if(cursor_hidden_owner)return;
    if(!cursor.visible)cursor_restore();else if(cursor.x!=x || cursor.y!=y){struct cb_overlay_move q={(int32_t)cursor.x,(int32_t)cursor.y};CB_REQUIRE(cursor_call(CB_OVERLAY_MOVE,&q,sizeof(q))==CB_OK);}
}
