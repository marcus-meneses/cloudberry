#include "../../sdk/system.h"
#include "../../sdk/application.h"
static unsigned char frame[CB_FRAME_LIMIT],result[CB_FRAME_LIMIT];
#define COPY cb_startup_copy
static cb_handle_t applications;
static cb_status_t call(unsigned operation,const void *data,size_t bytes){struct cb_call_header h=cb_call(applications,(struct cb_interface_id)CB_APPLICATIONS_INTERFACE_ID,operation,bytes);COPY(frame,&h,sizeof(h));COPY(frame+sizeof(h),data,bytes);uint64_t n;cb_status_t status=cb_system_invoke(frame,sizeof(h)+bytes,result,sizeof(result),&n);struct cb_reply_header reply;COPY(&reply,result,sizeof(reply));if(reply.request.serial && cb_system_request_release(reply.request)!=CB_OK)cb_exit(CB_RESOURCE_FAILURE);return status;}
__attribute__((noreturn)) void _start(const struct cb_startup *packet){
    struct cb_application_start start;if(!cb_application_start(packet,&start) || start.header.argc<2)cb_exit(CB_BAD_REQUEST);
    const char *target=cb_application_argument(&start,1);size_t name_bytes=0;while(target[name_bytes])++name_bytes;
    const unsigned char *arguments=(const void *)target;size_t arguments_bytes=start.header.arguments_bytes-(size_t)(arguments-start.arguments);
    struct cb_application_launch launch={.version={sizeof(launch),1,0},.name_bytes=name_bytes,.argc=start.header.argc-1,.arguments_bytes=arguments_bytes,.cwd_bytes=start.header.cwd_bytes};
    size_t bytes=sizeof(launch)+name_bytes+arguments_bytes+launch.cwd_bytes;if(bytes>CB_FRAME_LIMIT-512-sizeof(struct cb_call_header))cb_exit(CB_BAD_REQUEST);
    unsigned char payload[CB_FRAME_LIMIT];COPY(payload,&launch,sizeof(launch));COPY(payload+sizeof(launch),target,name_bytes);COPY(payload+sizeof(launch)+name_bytes,arguments,arguments_bytes);COPY(payload+sizeof(launch)+name_bytes+arguments_bytes,start.cwd,launch.cwd_bytes);
    if(cb_system_get_handle(CB_APPLICATIONS_NAME,sizeof(CB_APPLICATIONS_NAME)-1,&(struct cb_interface_id)CB_APPLICATIONS_INTERFACE_ID,1,0,&applications)!=CB_OK)cb_exit(CB_DENIED);
    cb_status_t status=call(CB_APPLICATION_LAUNCH,payload,bytes);if(status)cb_exit(status);struct cb_application_ref child;COPY(&child,result+sizeof(struct cb_reply_header),sizeof(child));
    status=call(CB_APPLICATION_WAIT,&child,sizeof(child));if(status)cb_exit(status);struct cb_application_status ended;COPY(&ended,result+sizeof(struct cb_reply_header),sizeof(ended));
    if(call(CB_APPLICATION_RELEASE,&child,sizeof(child))!=CB_OK || cb_system_release(applications)!=CB_OK)cb_exit(CB_RESOURCE_FAILURE);
    cb_exit(ended.state==CB_APPLICATION_EXITED?ended.exit_code:128+ended.fault_vector);
}
