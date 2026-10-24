#include "platform.h"
#include <string.h>
struct ed_platform platform;
unsigned ep_failed_op;
uint64_t ep_actual,ep_expected;
static cb_status_t acquire(const char *name,struct cb_interface_id id,cb_handle_t *handle){
    return cb_system_get_handle(name,strlen(name),&id,1,0,handle);
}
static cb_status_t result(struct cb_client *c,cb_status_t status,void *out,size_t n){
    if(status)return status;
    struct cb_reply_header h;
    memcpy(&h,c->result,sizeof(h));
    if(c->bytes<sizeof(h) || h.result_bytes!=n || sizeof(h)+n>c->bytes)return CB_BAD_REQUEST;
    if(n)memcpy(out,cb_result(c),n);
    return CB_OK;
}
cb_status_t ep_gfx(unsigned op,const void *data,size_t n,void *out,size_t size){
    cb_status_t s=result(&platform.client,cb_invoke(&platform.client,platform.graphics,(struct cb_interface_id)CB_GRAPHICS_INTERFACE_ID,op,data,n,0),out,size);
    if(s){
        struct cb_reply_header h;
        memcpy(&h,platform.client.result,sizeof(h));
        ep_failed_op=op;
        ep_actual=h.result_bytes;
        ep_expected=size;
    }
    return s;
}
cb_status_t ep_cursor(int hide){
    if(!platform.mouse || platform.hidden==(unsigned)hide)return CB_OK;
    cb_status_t s=cb_invoke(&platform.client,platform.mouse,(struct cb_interface_id)CB_MOUSE_INTERFACE_ID,hide?CB_MOUSE_CURSOR_HIDE:CB_MOUSE_CURSOR_SHOW,NULL,0,0);
    if(!s)platform.hidden=hide;
    return s;
}
cb_status_t ep_init(void){
    cb_status_t s=acquire(CB_GRAPHICS_NAME,(struct cb_interface_id)CB_GRAPHICS_INTERFACE_ID,&platform.graphics);
    if(s)return s;
    s=acquire(CB_KEYBOARD_NAME,(struct cb_interface_id)CB_KEYBOARD_INTERFACE_ID,&platform.keyboard);
    if(s)return s;
    s=acquire(CB_FILESYSTEM_NAME,(struct cb_interface_id)CB_FILESYSTEM_INTERFACE_ID,&platform.filesystem);
    if(s)return s;
    struct cb_keyboard_focus focus;
    s=result(&platform.client,cb_invoke(&platform.client,platform.keyboard,(struct cb_interface_id)CB_KEYBOARD_INTERFACE_ID,CB_KEYBOARD_FOCUS_CLAIM,NULL,0,0),&focus,sizeof(focus));
    if(s)return s;
    platform.focused=1;
    platform.keyread=(struct cb_keyboard_read_focus){
        .base={
            .version={
                sizeof(platform.keyread),1,1
            },.provider_epoch=focus.provider_epoch,.after_sequence=focus.next_sequence,.max_events=32
        },.focus_generation=focus.generation
    };
    s=acquire(CB_MOUSE_NAME,(struct cb_interface_id)CB_MOUSE_INTERFACE_ID,&platform.mouse);
    if(s==CB_NOT_FOUND || s==CB_UNAVAILABLE){
        platform.mouse=0;
        return CB_OK;
    }
    if(s)return s;
    uint64_t index=0;
    s=cb_invoke(&platform.client,platform.mouse,(struct cb_interface_id)CB_MOUSE_INTERFACE_ID,CB_MOUSE_STATE,&index,sizeof(index),0);
    if(s)return s;
    struct cb_mouse_state state;
    if(platform.client.bytes<sizeof(struct cb_reply_header)+sizeof(state))return CB_BAD_REQUEST;
    memcpy(&state,cb_result(&platform.client),sizeof(state));
    if(state.version.bytes!=sizeof(state) || state.version.major!=1)return CB_BAD_REQUEST;
    platform.mouseread=(struct cb_mouse_read){
        .version={
            sizeof(platform.mouseread),1,0
        },.provider_epoch=state.provider_epoch,.after_sequence=state.last_sequence,.max_events=32
    };
    return CB_OK;
}
cb_status_t ep_arm(void){
    cb_status_t s=0;
    if(!platform.keys.pending.serial)s=cb_submit(&platform.keys,platform.keyboard,(struct cb_interface_id)CB_KEYBOARD_INTERFACE_ID,CB_KEYBOARD_READ,&platform.keyread,sizeof(platform.keyread));
    if(!s&&platform.mouse&&!platform.moves.pending.serial)s=cb_submit(&platform.moves,platform.mouse,(struct cb_interface_id)CB_MOUSE_INTERFACE_ID,CB_MOUSE_READ_CURSOR,&platform.mouseread,sizeof(platform.mouseread));
    return s;
}
cb_status_t ep_wait(unsigned *which){
    struct cb_wait_item items[2]={
        {
            .kind=CB_WAIT_REQUEST,.request=platform.keys.pending
        },{
            .kind=CB_WAIT_REQUEST,.request=platform.moves.pending
        }
    };
    uint64_t ready;
    cb_status_t s=cb_system_wait(items,platform.mouse?2:1,0,&ready);
    if(!s){
        if(ready>=(platform.mouse?2u:1u))return CB_BAD_REQUEST;
        *which=(unsigned)ready;
    }
    return s;
}
cb_status_t ep_fs(unsigned op,const char *path,uint64_t cursor,void *out,size_t capacity,size_t *length){
    cb_status_t s=cb_fs_call(&platform.client,platform.filesystem,op,path,0,cursor);
    if(s)return s;
    struct cb_reply_header h;
    memcpy(&h,platform.client.result,sizeof(h));
    if(platform.client.bytes<sizeof(h)||h.result_bytes>capacity||sizeof(h)+h.result_bytes>platform.client.bytes)return CB_BAD_REQUEST;
    if(h.result_bytes)memcpy(out,cb_result(&platform.client),h.result_bytes);
    if(length)*length=h.result_bytes;
    return CB_OK;
}
cb_status_t ep_load(const char *path,unsigned char **data,size_t *n){
    return cb_runtime_load(cb_libc_runtime(),cb_libc_cwd(),path,data,n);
}
void ep_free_file(void *p){
    if(p)cb_heap_free(&cb_libc_runtime()->heap,p);
}
cb_status_t ep_save(const char *path,const void *data,size_t n){
    cb_handle_t file;
    struct cb_runtime *r=cb_libc_runtime();
    cb_status_t s=cb_runtime_open(r,cb_libc_cwd(),path,CB_FILE_RECREATE,&file);
    if(s)return s;
    s=cb_runtime_write_all(r,file,data,n);
    cb_status_t close=cb_runtime_close(r,file);
    return s?s:close;
}
void ep_close(void){
    struct cb_abandon_result abandoned;
    if(platform.keys.pending.serial)(void)cb_abandon(&platform.keys,&abandoned);
    if(platform.moves.pending.serial)(void)cb_abandon(&platform.moves,&abandoned);
    (void)ep_cursor(0);
    if(platform.focused)(void)cb_invoke(&platform.client,platform.keyboard,(struct cb_interface_id)CB_KEYBOARD_INTERFACE_ID,CB_KEYBOARD_FOCUS_RELEASE,NULL,0,0);
    cb_handle_t *handles[]={
        &platform.mouse,&platform.keyboard,&platform.graphics,&platform.filesystem
    };
    for(unsigned i=0;i<4;i++)if(*handles[i]){
        (void)cb_system_release(*handles[i]);
        *handles[i]=0;
    }
}
