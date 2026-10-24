#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../sdk/service.h"
static cb_status_t mock_invoke(struct cb_client *,cb_handle_t,struct cb_interface_id,unsigned,const void *,size_t,uint64_t);
#define cb_invoke mock_invoke
#define _start mouse_provider_start
#include "../handlers/mouse/mouse.c"
#undef _start
#undef cb_invoke
static uint64_t focused=42;static unsigned watches,hides,moves,shapes,watch_failure;
static cb_status_t mock_invoke(struct cb_client *c,cb_handle_t h,struct cb_interface_id id,unsigned op,const void *payload,size_t bytes,uint64_t deadline){
    (void)h;(void)deadline;const struct cb_interface_id io_id=CB_IO_INTERFACE_ID;
    if(!memcmp(&id,&io_id,sizeof(id))){
        if(op==CB_IO_INPUT_FOCUS){struct cb_io_input_focus focus={focused,1,7,7};memcpy(cb_result(c),&focus,sizeof(focus));return CB_OK;}
        assert(op==CB_IO_OWNER_WATCH && bytes==sizeof(struct cb_io_owner_watch));struct cb_io_owner_watch w;memcpy(&w,payload,sizeof(w));assert(w.token==CURSOR_WATCH);if(w.enable){if(watch_failure)return CB_RESOURCE_FAILURE;++watches;}else {assert(watches);--watches;}return CB_OK;
    }
    if(op==CB_OVERLAY_HIDE){++hides;return CB_OK;}if(op==CB_OVERLAY_MOVE){++moves;struct cb_overlay_move q;memcpy(&q,payload,sizeof(q));assert(q.x==(int32_t)cursor.x && q.y==(int32_t)cursor.y);return CB_OK;}assert(op==CB_OVERLAY_SET);++shapes;return CB_OK;
}
int main(void){
    struct mouse m={0};devices=&m;cursor=(struct cb_mouse_cursor){20,30,640,480,0,0};cursor_restore();assert(cursor.visible && shapes==1);
    assert(cursor_control(99,1)==CB_DENIED && !watches);
    assert(!cursor_control(42,1) && cursor_hidden_owner==42 && !cursor.visible && watches==1 && hides==1);assert(!cursor_control(42,1) && watches==1);
    struct mouse_hid_input value={.dx=5,.dy=7,.flags=CB_MOUSE_RELATIVE};cursor_update(&m,CB_MOUSE_INPUT,&value);assert(cursor.x==25 && cursor.y==37 && !cursor.visible && !moves && shapes==1);
    assert(cursor_control(99,0)==CB_DENIED && !cursor.visible);assert(!cursor_control(42,0) && cursor.visible && !watches && moves==1 && shapes==1);
    assert(!cursor_control(42,1));cursor_end_hide();assert(cursor.visible && !cursor_hidden_owner && !watches);
    assert(!cursor_control(42,1));focused=43;value.dx=value.dy=0;cursor_update(&m,CB_MOUSE_INPUT,&value);assert(cursor.visible && !cursor_hidden_owner && !watches);
    watch_failure=1;assert(cursor_control(43,1)==CB_RESOURCE_FAILURE && cursor.visible && !cursor_hidden_owner && !watches);watch_failure=0;
    assert(!cursor_control(43,1));devices=NULL;assert(!cursor_control(43,0) && !cursor.visible && !watches);devices=&m;cursor_update(&m,CB_MOUSE_ADDED,NULL);assert(cursor.visible && shapes==1);
    puts("PASS mouse cursor: foreground authority, hidden motion, bitmap reuse, idempotent hide/show, owner/focus restoration and failed-watch rollback");
}
