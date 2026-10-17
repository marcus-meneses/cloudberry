#include "../../sdk/system.h"
#include "../../interfaces/io.h"
static const struct cb_interface_id test_interface={{1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16}};
static unsigned char input[CB_FRAME_LIMIT],output[CB_FRAME_LIMIT];
#define CHECK(x) do{if(!(x))cb_exit(__LINE__);}while(0)
static void copy(void *dst,const void *src,size_t n){unsigned char *d=dst;const unsigned char *s=src;for(size_t i=0;i<n;++i)d[i]=s[i];}
static uint64_t make_publication(void){
    struct cb_publication p={.version={sizeof(p),1,0},.interface=test_interface,.interface_major=1,.name_bytes=5};
    struct cb_description d={.version={sizeof(d),1,0},.interface=test_interface,.interface_major=1,.operation_count=2,.facts_bytes=2};
    size_t off=sizeof(p)+5+sizeof(d);const char *names[]={"ping","secret"};
    for(unsigned i=0;i<2;++i){uint64_t n=i?6:4;struct cb_operation_description op={.version={sizeof(op),1,0},.operation=i+1,.name_bytes=n,.request_schema_bytes=2,.reply_schema_bytes=2};copy(input+off,&op,sizeof(op));off+=sizeof(op);copy(input+off,names[i],n);off+=n;copy(input+off,"{}{}",4);off+=4;}
    d.operations_bytes=off-sizeof(p)-5-sizeof(d);copy(input+off,"{}",2);off+=2;p.description_bytes=off-sizeof(p)-5;copy(input,&p,sizeof(p));copy(input+sizeof(p),"@echo",5);copy(input+sizeof(p)+5,&d,sizeof(d));return off;
}
__attribute__((noreturn)) void _start(uint64_t role){
    if(role){const struct cb_interface_id io=CB_IO_INTERFACE_ID;cb_handle_t clock;CHECK(cb_system_get_handle(CB_IO_NAME,sizeof(CB_IO_NAME)-1,&io,1,0,&clock)==CB_OK);
        struct cb_call_header clock_call=cb_call(clock,io,CB_IO_NOW_NS,0);uint64_t size;CHECK(cb_system_invoke(&clock_call,sizeof(clock_call),output,sizeof(output),&size)==CB_OK && size==sizeof(struct cb_reply_header)+8);
        clock_call.operation=CB_IO_KEYBOARD_SUBSCRIBE;CHECK(cb_system_invoke(&clock_call,sizeof(clock_call),output,sizeof(output),&size)==CB_DENIED);CHECK(cb_system_release(clock)==CB_OK);
        uint64_t n=make_publication();cb_handle_t published;CHECK(cb_system_publish(input,n,&published)==CB_OK);CHECK(cb_system_publish(input,n,NULL)==CB_NAME_IN_USE);
        uint64_t bytes;CHECK(cb_system_receive(input,sizeof(input),&bytes,0)==CB_OK);struct cb_delivery_header d;copy(&d,input,sizeof(d));CHECK(d.call.flags==CB_CALL_PROVIDER_DELIVERY && d.call.handle==published && d.call.operation==1 && d.call.payload_bytes==8);
        uint64_t payload;copy(&payload,input+d.call.version.bytes+d.context_bytes,8);CHECK(payload==41);
        struct {struct cb_reply_header h;uint64_t result,provider_token;} reply={.h={.version={sizeof(struct cb_reply_header),1,0},.request=d.call.request,.outcome=CB_COMMITTED,.provider_epoch=d.provider_epoch,.result_bytes=16},.result=42,.provider_token=published};
        CHECK(cb_system_reply(&reply,sizeof(reply))==CB_OK);cb_exit(0);
    }
    const struct cb_interface_id memory=CB_MEMORY_INTERFACE_ID;cb_handle_t mem,echo;
    CHECK(cb_system_get_handle(CB_MEMORY_NAME,sizeof(CB_MEMORY_NAME)-1,&memory,1,0,&mem)==CB_OK);
    CHECK(cb_system_get_handle("/@ungranted",12,&memory,1,0,&echo)==CB_DENIED);
    struct {struct cb_call_header h;struct cb_memory_request m;} allocation={.h=cb_call(mem,memory,CB_MEMORY_ALLOCATE,sizeof(struct cb_memory_request)),.m={.bytes=5000}};
    uint64_t bytes;CHECK(cb_system_invoke(&allocation,sizeof(allocation),output,sizeof(output),&bytes)==CB_OK);struct cb_memory_result region;copy(&region,output+sizeof(struct cb_reply_header),sizeof(region));CHECK(region.address && region.bytes==5000);
    unsigned char *data=(void *)(uintptr_t)region.address;for(unsigned i=0;i<5000;++i)CHECK(data[i]==0);data[4999]=42;
    allocation.h=cb_call(mem,memory,CB_MEMORY_FREE,sizeof(allocation.m));allocation.m=(struct cb_memory_request){.address=region.address};CHECK(cb_system_invoke(&allocation,sizeof(allocation),output,sizeof(output),&bytes)==CB_OK);
    CHECK(cb_system_get_handle("@echo",5,&test_interface,1,0,&echo)==CB_OK);
    struct {struct cb_call_header h;struct cb_describe_request d;} describe={.h=cb_call(echo,(struct cb_interface_id)CB_HANDLE_INTERFACE_ID,CB_HANDLE_DESCRIBE,sizeof(struct cb_describe_request)),.d={.version={sizeof(struct cb_describe_request),1,0},.interface=test_interface}};
    CHECK(cb_system_invoke(&describe,sizeof(describe),output,sizeof(output),&bytes)==CB_OK);struct cb_description desc;copy(&desc,output+sizeof(struct cb_reply_header),sizeof(desc));CHECK(desc.operation_count==1);
    struct {struct cb_call_header h;uint64_t value;} call={.h=cb_call(echo,test_interface,2,8),.value=41};
    CHECK(cb_system_invoke(&call,sizeof(call),output,sizeof(output),&bytes)==CB_DENIED);struct cb_error_header denied;copy(&denied,output+sizeof(struct cb_reply_header),sizeof(denied));CHECK(denied.original_payload_bytes==8 && denied.trace_bytes);
    call.h.operation=1;call.h.flags=CB_CALL_PROVIDER_DELIVERY|CB_CALL_SYSTEM_EVENT;CHECK(cb_system_invoke(&call,sizeof(call),output,sizeof(output),&bytes)==CB_BAD_REQUEST);call.h.flags=0;call.h.request=(struct cb_request_id){UINT64_MAX,1};CHECK(cb_system_invoke(&call,sizeof(call),output,sizeof(output),&bytes)==CB_BAD_REQUEST);call.h.request=(struct cb_request_id){0};
    struct cb_system_gate malformed={.bytes=sizeof(malformed),.operation=CB_SYSTEM_INVOKE,.input=(uintptr_t)&call,.input_bytes=sizeof(call),.output=region.address,.output_capacity=1024};CHECK(cb_system_gate(&malformed)==CB_BAD_REQUEST);
    malformed.input=0x300000;malformed.output=(uintptr_t)output;CHECK(cb_system_gate(&malformed)==CB_BAD_REQUEST);
    struct cb_reply_header short_reply;CHECK(cb_system_invoke(&call,sizeof(call),&short_reply,sizeof(short_reply),&bytes)==CB_BUFFER_TOO_SMALL && short_reply.request.serial && bytes>sizeof(short_reply));
    call.h.request=short_reply.request;call.h.attempt=1;CHECK(cb_system_invoke(&call,sizeof(call),output,sizeof(output),&bytes)==CB_OK);uint64_t answer,provider_token;copy(&answer,output+sizeof(struct cb_reply_header),8);copy(&provider_token,output+sizeof(struct cb_reply_header)+8,8);CHECK(answer==42);CHECK(cb_system_reply(output,bytes)==CB_BAD_REQUEST);
    call.h=cb_call(provider_token,test_interface,1,8);CHECK(cb_system_invoke(&call,sizeof(call),output,sizeof(output),&bytes)==CB_BAD_HANDLE);
    call.h=cb_call(echo,test_interface,1,8);CHECK(cb_system_invoke(&call,sizeof(call),output,sizeof(output),&bytes)==CB_PROVIDER_FAILED);struct cb_reply_header failure;struct cb_error_header error;copy(&failure,output,sizeof(failure));copy(&error,output+sizeof(failure),sizeof(error));uint64_t original;copy(&original,output+sizeof(failure)+sizeof(error),8);CHECK(original==41 && error.original_payload_bytes==8 && failure.outcome==CB_NOT_DISPATCHED && error.trace_bytes);
    CHECK(cb_system_release(echo)==CB_OK && cb_system_release(echo)==CB_BAD_HANDLE);CHECK(cb_system_release(mem)==CB_OK);cb_exit(0);
}
