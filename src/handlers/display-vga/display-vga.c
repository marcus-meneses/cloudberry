#include "../../sdk/service.h"
#include "../../interfaces/display.h"
#include "../../interfaces/io.h"
static struct cb_client client,reply;
static unsigned char input[CB_FRAME_LIMIT];
__attribute__((noreturn)) void _start(const struct cb_startup *startup){
    (void)startup;cb_handle_t io=cb_acquire(CB_IO_NAME,(struct cb_interface_id)CB_IO_INTERFACE_ID);
    static const char *names[]={"info","fill","blit","copy"};cb_handle_t pub=cb_publish(&client,CB_DISPLAY_NAME,(struct cb_interface_id)CB_DISPLAY_INTERFACE_ID,names,4);
    for(;;){uint64_t bytes;CB_REQUIRE(cb_system_receive(input,sizeof(input),&bytes,0)==CB_OK && bytes>=sizeof(struct cb_delivery_header));struct cb_delivery_header d;cb_copy(&d,input,sizeof(d));
        if(d.call.flags&CB_CALL_SYSTEM_EVENT){if(d.call.operation==CB_HANDLE_RELEASE){cb_system_release(pub);cb_system_release(io);cb_exit(0);}continue;}
        cb_status_t status=CB_BAD_REQUEST;const void *out=NULL;size_t n=0;
        if(d.call.handle==pub && d.context_bytes<=bytes-sizeof(d) && d.call.payload_bytes==bytes-sizeof(d)-d.context_bytes && d.call.operation>=1 && d.call.operation<=4){struct cb_admission caller;if(!cb_admission_parse(input+sizeof(d),d.context_bytes,&caller))status=CB_DENIED;else {status=CB_OK;if(d.call.operation!=CB_DISPLAY_INFO){struct cb_io_console_access access={caller.process,CB_CONSOLE_DISPLAY,0};status=cb_invoke(&client,io,(struct cb_interface_id)CB_IO_INTERFACE_ID,CB_IO_CONSOLE_ACCESS,&access,sizeof(access),0);}if(!status)status=cb_invoke(&client,io,(struct cb_interface_id)CB_IO_INTERFACE_ID,d.call.operation==CB_DISPLAY_COPY?CB_IO_DISPLAY_COPY:CB_IO_DISPLAY_INFO+d.call.operation-1,input+sizeof(d)+d.context_bytes,d.call.payload_bytes,0);if(!status){struct cb_reply_header h;cb_copy(&h,client.result,sizeof(h));out=cb_result(&client);n=h.result_bytes;}}}
        CB_REQUIRE(cb_respond(&reply,&d,status,out,n)==CB_OK);
    }
}
