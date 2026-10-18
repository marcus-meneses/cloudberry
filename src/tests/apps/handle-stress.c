#include "../../sdk/system.h"
#include "../../interfaces/io.h"
static const struct cb_interface_id interface={{8,7,6,5,4,3,2,1}};
static unsigned char input[CB_FRAME_LIMIT],output[CB_FRAME_LIMIT];
static cb_handle_t clock_handle;
enum {CLIENTS=3,ROUNDS=12,SLOW=0xdead01,QUEUED=0xdead02};
#define CHECK(x) do{if(!(x))cb_exit(__LINE__);}while(0)
static void copy(void *d,const void *s,size_t n){unsigned char *to=d;const unsigned char *from=s;for(size_t i=0;i<n;++i)to[i]=from[i];}
static struct cb_reply_header header(void){struct cb_reply_header h;copy(&h,output,sizeof(h));return h;}
static void forget(void){CHECK(cb_system_request_release(header().request)==CB_OK);}
static uint64_t now(void){struct cb_call_header c=cb_call(clock_handle,(struct cb_interface_id)CB_IO_INTERFACE_ID,CB_IO_NOW_NS,0);uint64_t bytes,time;CHECK(cb_system_invoke(&c,sizeof(c),output,sizeof(output),&bytes)==CB_OK);copy(&time,output+sizeof(struct cb_reply_header),8);forget();return time;}
static void memory(void){const struct cb_interface_id id=CB_MEMORY_INTERFACE_ID;cb_handle_t mem;CHECK(cb_system_get_handle(CB_MEMORY_NAME,sizeof(CB_MEMORY_NAME)-1,&id,1,0,&mem)==CB_OK);
    for(unsigned i=0;i<8;++i){struct {struct cb_call_header c;struct cb_memory_request m;} r={.c=cb_call(mem,id,CB_MEMORY_ALLOCATE,sizeof(r.m)),.m={.bytes=4096}};uint64_t bytes;CHECK(cb_system_invoke(&r,sizeof(r),output,sizeof(output),&bytes)==CB_OK);struct cb_memory_result allocation;copy(&allocation,output+sizeof(struct cb_reply_header),sizeof(allocation));forget();unsigned char *p=(void *)(uintptr_t)allocation.address;CHECK(p[0]==0 && p[4095]==0);p[4095]=99;r.c=cb_call(mem,id,CB_MEMORY_FREE,sizeof(r.m));r.m=(struct cb_memory_request){.address=allocation.address};CHECK(cb_system_invoke(&r,sizeof(r),output,sizeof(output),&bytes)==CB_OK);forget();}
    CHECK(cb_system_release(mem)==CB_OK);
}
static void server(void){
    struct cb_publication p={.version={sizeof(p),1,0},.interface=interface,.interface_major=1,.name_bytes=7};
    struct cb_description d={.version={sizeof(d),1,0},.interface=interface,.interface_major=1,.operation_count=1,.facts_bytes=2};
    struct cb_operation_description op={.version={sizeof(op),1,0},.operation=1,.name_bytes=4,.request_schema_bytes=2,.reply_schema_bytes=2};d.operations_bytes=sizeof(op)+8;p.description_bytes=sizeof(d)+d.operations_bytes+2;
    size_t at=0;copy(input+at,&p,sizeof(p));at+=sizeof(p);copy(input+at,"@stress",7);at+=7;copy(input+at,&d,sizeof(d));at+=sizeof(d);copy(input+at,&op,sizeof(op));at+=sizeof(op);copy(input+at,"echo{}{}{}",10);at+=10;
    cb_handle_t cap;CHECK(cb_system_publish(input,at,&cap)==CB_OK);
    for(unsigned i=0;i<CLIENTS*ROUNDS+1;++i){uint64_t bytes;CHECK(cb_system_receive(input,sizeof(input),&bytes,0)==CB_OK);struct cb_delivery_header delivery;copy(&delivery,input,sizeof(delivery));CHECK(delivery.call.operation==1 && delivery.call.handle==cap && delivery.call.payload_bytes>=8 && delivery.call.payload_bytes<=4096);uint64_t value;copy(&value,input+sizeof(delivery)+delivery.context_bytes,8);CHECK(value!=QUEUED);
        if(i==CLIENTS*ROUNDS){CHECK(value==SLOW);uint64_t until=now()+UINT64_C(1500000000);while(now()<until)__asm__ volatile("pause");}
        else {CHECK(value!=SLOW);if(i%8==0){uint64_t until=now()+UINT64_C(2000000);while(now()<until)__asm__ volatile("pause");}}
        struct cb_reply_header reply={.version={sizeof(reply),1,0},.request=delivery.call.request,.provider_epoch=delivery.provider_epoch,.outcome=CB_COMMITTED,.result_bytes=delivery.call.payload_bytes};copy(output,&reply,sizeof(reply));copy(output+sizeof(reply),input+sizeof(delivery)+delivery.context_bytes,reply.result_bytes);CHECK(cb_system_reply(output,sizeof(reply)+reply.result_bytes)==CB_OK);
    }
    CHECK(cb_system_release(cap)==CB_OK);
}
__attribute__((noreturn)) void _start(uint64_t role){
    const struct cb_interface_id io=CB_IO_INTERFACE_ID;CHECK(cb_system_get_handle(CB_IO_NAME,sizeof(CB_IO_NAME)-1,&io,1,0,&clock_handle)==CB_OK);
    if(role==1){server();CHECK(cb_system_release(clock_handle)==CB_OK);cb_exit(0);}
    if(role==7){memory();CHECK(cb_system_release(clock_handle)==CB_OK);cb_exit(0);}
    cb_handle_t echo;CHECK(cb_system_get_handle("@stress",7,&interface,1,0,&echo)==CB_OK);
    if(role>=2 && role<=4){memory();for(unsigned trial=0;trial<ROUNDS;++trial){struct cb_call_header call=cb_call(echo,interface,1,4096);copy(input,&call,sizeof(call));for(unsigned i=0;i<4096;++i)input[sizeof(call)+i]=(unsigned char)(role*31+trial*17+i);uint64_t value=role*100+trial;copy(input+sizeof(call),&value,8);uint64_t bytes;CHECK(cb_system_invoke(input,sizeof(call)+4096,output,sizeof(output),&bytes)==CB_OK && bytes==sizeof(struct cb_reply_header)+4096);for(unsigned i=0;i<4096;++i)CHECK(input[sizeof(call)+i]==output[sizeof(struct cb_reply_header)+i]);CHECK(header().outcome==CB_COMMITTED);forget();}}
    else {CHECK(role==5 || role==6);struct {struct cb_call_header c;uint64_t value;} r={.c=cb_call(echo,interface,1,8),.value=role==5?SLOW:QUEUED};r.c.deadline_ns=now()+(role==5?UINT64_C(100000000):UINT64_C(20000000));uint64_t bytes;CHECK(cb_system_invoke(&r,sizeof(r),output,sizeof(output),&bytes)==CB_TIMEOUT);struct cb_reply_header timed=header();CHECK(timed.outcome==(role==5?CB_OUTCOME_UNKNOWN:CB_NOT_DISPATCHED));struct cb_error_header error;copy(&error,output+sizeof(timed),sizeof(error));uint64_t original;copy(&original,output+sizeof(timed)+sizeof(error),8);CHECK(error.original_payload_bytes==8 && original==r.value);
        if(role==6)CHECK(cb_system_request_release(timed.request)==CB_OK);
        else {CHECK(cb_system_request_release(timed.request)==CB_BUSY);uint64_t until=now()+UINT64_C(2000000000);CHECK(cb_system_receive(input,sizeof(input),&bytes,until)==CB_TIMEOUT);r.c.request=timed.request;r.c.attempt=1;r.c.deadline_ns=0;CHECK(cb_system_invoke(&r,sizeof(r),output,sizeof(output),&bytes)==CB_OK && header().outcome==CB_COMMITTED);copy(&original,output+sizeof(struct cb_reply_header),8);CHECK(original==SLOW);forget();}
    }
    CHECK(cb_system_release(echo)==CB_OK && cb_system_release(clock_handle)==CB_OK);cb_exit(0);
}
