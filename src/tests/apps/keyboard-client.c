#include "../../sdk/system.h"
#include "../../interfaces/keyboard.h"
#include "../../interfaces/io.h"
static unsigned char output[CB_FRAME_LIMIT];
#define CHECK(x) do{if(!(x))cb_exit(__LINE__);}while(0)
static void copy(void *dst,const void *src,size_t n){unsigned char *d=dst;const unsigned char *s=src;for(size_t i=0;i<n;++i)d[i]=s[i];}
static void forget(void){struct cb_reply_header h;copy(&h,output,sizeof(h));if(h.request.serial)CHECK(cb_system_request_release(h.request)==CB_OK);}
static struct cb_keyboard_state state(cb_handle_t handle){struct cb_call_header call=cb_call(handle,(struct cb_interface_id)CB_KEYBOARD_INTERFACE_ID,CB_KEYBOARD_STATE,0);uint64_t bytes;CHECK(cb_system_invoke(&call,sizeof(call),output,sizeof(output),&bytes)==CB_OK);struct cb_keyboard_state s;copy(&s,output+sizeof(struct cb_reply_header),sizeof(s));CHECK(s.version.bytes==sizeof(s) && s.provider_epoch);forget();return s;}
__attribute__((noreturn)) void _start(uint64_t role){
    struct cb_interface_id interface=CB_KEYBOARD_INTERFACE_ID;cb_handle_t keyboard;
    cb_status_t acquired=cb_system_get_handle(CB_KEYBOARD_NAME,sizeof(CB_KEYBOARD_NAME)-1,&interface,1,0,&keyboard);
    if(role==3){CHECK(acquired==CB_DENIED);cb_exit(0);}CHECK(acquired==CB_OK);
    struct cb_interface_id io=CB_IO_INTERFACE_ID;cb_handle_t denied;CHECK(cb_system_get_handle(CB_IO_NAME,sizeof(CB_IO_NAME)-1,&io,1,0,&denied)==CB_DENIED);
    struct {struct cb_call_header h;struct cb_describe_request d;} reflection={.h=cb_call(keyboard,(struct cb_interface_id)CB_HANDLE_INTERFACE_ID,CB_HANDLE_DESCRIBE,sizeof(struct cb_describe_request)),.d={.version={sizeof(struct cb_describe_request),1,0},.interface=CB_KEYBOARD_INTERFACE_ID}};uint64_t bytes;
    CHECK(cb_system_invoke(&reflection,sizeof(reflection),output,sizeof(output),&bytes)==CB_OK);struct cb_description description;copy(&description,output+sizeof(struct cb_reply_header),sizeof(description));CHECK(description.operation_count==(role==1?1:2));forget();
    if(role==1){struct cb_call_header call=cb_call(keyboard,interface,CB_KEYBOARD_STATE,0);CHECK(cb_system_invoke(&call,sizeof(call),output,sizeof(output),&bytes)==CB_DENIED);}
    else{struct cb_keyboard_state s=state(keyboard);CHECK(s.count==0);}
    struct {struct cb_call_header h;struct cb_keyboard_read read;} read={.h=cb_call(keyboard,interface,CB_KEYBOARD_READ,sizeof(struct cb_keyboard_read)),.read={.version={sizeof(struct cb_keyboard_read),1,0},.provider_epoch=UINT64_MAX,.max_events=32}};
    CHECK(cb_system_invoke(&read,sizeof(read),output,sizeof(output),&bytes)==CB_INPUT_RESYNC);struct cb_keyboard_batch batch;copy(&batch,output+sizeof(struct cb_reply_header),sizeof(batch));CHECK(batch.provider_epoch && !batch.count);forget();read.read.provider_epoch=batch.provider_epoch;read.read.after_sequence=batch.next_sequence;
    int down=0,up=0,text=0,removed=0,added=0;uint64_t first_device=0;
    for(;;){read.h=cb_call(keyboard,interface,CB_KEYBOARD_READ,sizeof(read.read));CHECK(cb_system_invoke(&read,sizeof(read),output,sizeof(output),&bytes)==CB_OK);copy(&batch,output+sizeof(struct cb_reply_header),sizeof(batch));CHECK(batch.provider_epoch==read.read.provider_epoch && batch.count && batch.count<=32);
        for(unsigned i=0;i<batch.count;++i){struct cb_keyboard_event event;copy(&event,output+sizeof(struct cb_reply_header)+sizeof(batch)+i*sizeof(event),sizeof(event));CHECK(cb_keyboard_event_valid(&event) && event.sequence>read.read.after_sequence);read.read.after_sequence=event.sequence;
            if(role==2){if(event.kind==CB_KEY_DEVICE_REMOVED){removed=1;first_device=event.device_id;}if(event.kind==CB_KEY_DEVICE_ADDED && removed){CHECK(event.device_id!=first_device);added=1;}
                if(event.kind==CB_KEY_DOWN && event.usage==79){CHECK(removed && added && !event.modifiers);down=1;}if(event.kind==CB_KEY_UP && event.usage==79)up=1;
            }else{if(event.kind==CB_KEY_DOWN && event.usage==4)down=1;if(event.kind==CB_KEY_UP && event.usage==4)up=1;if(event.kind==CB_KEY_TEXT && event.codepoint=='a')text=1;}
        }
        CHECK(batch.next_sequence==read.read.after_sequence);forget();
        if(role==2 && added){struct cb_keyboard_state current=state(keyboard);CHECK(current.provider_epoch==batch.provider_epoch);if(!down)CHECK(!current.count);}
        if(down && up && (role==2 || text))break;
    }
    CHECK(cb_system_release(keyboard)==CB_OK);cb_exit(0);
}
