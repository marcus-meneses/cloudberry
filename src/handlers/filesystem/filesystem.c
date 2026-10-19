#include "../../sdk/service.h"
#include "../../interfaces/filesystem.h"
#include "../../interfaces/io.h"
static struct cb_client client,reply;
static unsigned char input[CB_FRAME_LIMIT],request[CB_FRAME_LIMIT];
__attribute__((noreturn)) void _start(const struct cb_startup *packet){(void)packet;cb_handle_t io=cb_acquire(CB_IO_NAME,(struct cb_interface_id)CB_IO_INTERFACE_ID);static const char *names[]={"open","stat","list","remove","mkdir"};cb_handle_t pub=cb_publish(&client,CB_FILESYSTEM_NAME,(struct cb_interface_id)CB_FILESYSTEM_INTERFACE_ID,names,5);
    for(;;){uint64_t bytes;CB_REQUIRE(cb_system_receive(input,sizeof(input),&bytes,0)==CB_OK && bytes>=sizeof(struct cb_delivery_header));struct cb_delivery_header d;cb_copy(&d,input,sizeof(d));if(d.call.flags&CB_CALL_SYSTEM_EVENT){if(d.call.operation==CB_HANDLE_RELEASE){cb_system_release(pub);cb_system_release(io);cb_exit(0);}continue;}struct cb_admission caller;cb_status_t status=CB_DENIED;size_t n=0;const void *out=NULL;
        CB_REQUIRE(d.context_bytes<=bytes-sizeof(d) && d.call.payload_bytes==bytes-sizeof(d)-d.context_bytes);
        if(d.call.handle==pub && cb_admission_parse(input+sizeof(d),d.context_bytes,&caller) && d.call.payload_bytes<sizeof(request)-1024){uint64_t op=d.call.operation;cb_copy(request,&caller.process,8);cb_copy(request+8,&op,8);cb_copy(request+16,caller.installation,16);cb_copy(request+32,input+sizeof(d)+d.context_bytes,d.call.payload_bytes);status=cb_invoke(&client,io,(struct cb_interface_id)CB_IO_INTERFACE_ID,CB_IO_FILESYSTEM,request,d.call.payload_bytes+32,0);if(!status){struct cb_reply_header h;cb_copy(&h,client.result,sizeof(h));out=cb_result(&client);n=h.result_bytes;}}
        CB_REQUIRE(cb_respond(&reply,&d,status,out,n)==CB_OK);
    }
}
