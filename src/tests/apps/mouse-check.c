#include "../../sdk/application.h"
#include "../../sdk/stream.h"
#include "../../interfaces/mouse.h"
static struct cb_client client,reader;
static cb_handle_t mouse;
static void print(const char *text){CB_REQUIRE(cb_write_all(&client,cb_standard(1),text,cb_length(text))==CB_OK);}
static unsigned device_count(void){unsigned count=0;for(uint64_t cursor=0;;++cursor){CB_REQUIRE(cb_invoke(&client,mouse,(struct cb_interface_id)CB_MOUSE_INTERFACE_ID,CB_MOUSE_STATE,&cursor,8,0)==CB_OK);struct cb_mouse_state state;cb_copy(&state,cb_result(&client),sizeof(state));if(!state.count)return count;++count;}}
__attribute__((noreturn)) void _start(const struct cb_startup *startup){(void)startup;mouse=cb_acquire(CB_MOUSE_NAME,(struct cb_interface_id)CB_MOUSE_INTERFACE_ID);uint64_t cursor=0;CB_REQUIRE(cb_invoke(&client,mouse,(struct cb_interface_id)CB_MOUSE_INTERFACE_ID,CB_MOUSE_STATE,&cursor,8,0)==CB_OK);struct cb_mouse_state state;cb_copy(&state,cb_result(&client),sizeof(state));CB_REQUIRE(state.count==1);struct cb_mouse_read read={.version={sizeof(read),1,0},.provider_epoch=state.provider_epoch,.after_sequence=state.last_sequence,.max_events=32};CB_REQUIRE(cb_submit(&reader,mouse,(struct cb_interface_id)CB_MOUSE_INTERFACE_ID,CB_MOUSE_READ,&read,sizeof(read))==CB_OK);print("MOUSE READY\n");unsigned motion=0,down=0,up=0,wheel=0,input=0,removed=0;uint64_t extra=0;
    for(;;){struct cb_wait_item wait={.kind=CB_WAIT_REQUEST,.request=reader.pending};uint64_t ready;CB_REQUIRE(cb_system_wait(&wait,1,0,&ready)==CB_OK);CB_REQUIRE(cb_collect(&reader)==CB_OK && reader.collected);struct cb_mouse_batch batch;cb_copy(&batch,cb_result(&reader),sizeof(batch));CB_REQUIRE(batch.count<=32);struct cb_mouse_event events[32];cb_copy(events,(const unsigned char *)cb_result(&reader)+sizeof(batch),batch.count*sizeof(*events));read.after_sequence=batch.next_sequence;
        for(unsigned i=0;i<batch.count;++i){struct cb_mouse_event *e=events+i;
            if(e->kind==CB_MOUSE_INPUT){if(e->dx==12 && e->dy==-7)motion=1;if(e->changed&1){if(e->buttons&1){down=1;if(e->device_id==extra)print("EXTRA BUTTON DOWN\n");}else up=1;}if(e->wheel_y)wheel=1;}
            if(!input && motion && down && up && wheel){input=1;print("MOUSE INPUT OK\n");}
            if(e->kind==CB_MOUSE_ADDED && input){if(removed){CB_REQUIRE(e->device_id!=extra && device_count()==2);print("MOUSE RECONNECTED\n");cb_exit(0);}extra=e->device_id;CB_REQUIRE(device_count()==2);print("MOUSE ADDED\n");}
            if(e->kind==CB_MOUSE_REMOVED && e->device_id==extra){CB_REQUIRE((e->changed&1) && !e->buttons && device_count()==1);removed=1;print("MOUSE REMOVED\n");}
            CB_REQUIRE(e->kind!=CB_MOUSE_ERROR && e->kind!=CB_MOUSE_RESET);
        }
        CB_REQUIRE(cb_submit(&reader,mouse,(struct cb_interface_id)CB_MOUSE_INTERFACE_ID,CB_MOUSE_READ,&read,sizeof(read))==CB_OK);
    }
}
