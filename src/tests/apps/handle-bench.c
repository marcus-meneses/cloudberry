#include "../../sdk/system.h"
#include "../../interfaces/io.h"
#include "../handle-bench.h"
static const struct cb_interface_id interface={{8,7,6,5,4,3,2,1}};
static unsigned char input[CB_FRAME_LIMIT],output[CB_FRAME_LIMIT];
static struct handle_bench_report report;
static cb_handle_t clock_handle;
#define CHECK(x) do{if(!(x))cb_exit(__LINE__);}while(0)
static void copy(void *d,const void *s,size_t n){unsigned char *out=d;const unsigned char *in=s;for(size_t i=0;i<n;++i)out[i]=in[i];}
static void forget(void){struct cb_reply_header r;copy(&r,output,sizeof(r));CHECK(cb_system_request_release(r.request)==CB_OK);}
static uint64_t clock_ns(void){struct cb_call_header c=cb_call(clock_handle,(struct cb_interface_id)CB_IO_INTERFACE_ID,CB_IO_NOW_NS,0);uint64_t bytes,time;CHECK(cb_system_invoke(&c,sizeof(c),output,sizeof(output),&bytes)==CB_OK);copy(&time,output+sizeof(struct cb_reply_header),8);forget();return time;}
static void server(void){
    struct cb_publication p={.version={sizeof(p),1,0},.interface=interface,.interface_major=1,.name_bytes=6};
    struct cb_description d={.version={sizeof(d),1,0},.interface=interface,.interface_major=1,.operation_count=1,.facts_bytes=2};
    struct cb_operation_description op={.version={sizeof(op),1,0},.operation=1,.name_bytes=4,.request_schema_bytes=2,.reply_schema_bytes=2};d.operations_bytes=sizeof(op)+8;p.description_bytes=sizeof(d)+d.operations_bytes+2;
    size_t at=0;copy(input+at,&p,sizeof(p));at+=sizeof(p);copy(input+at,"@bench",6);at+=6;copy(input+at,&d,sizeof(d));at+=sizeof(d);copy(input+at,&op,sizeof(op));at+=sizeof(op);copy(input+at,"echo{}{}{}",10);at+=10;
    cb_handle_t cap;CHECK(cb_system_publish(input,at,&cap)==CB_OK);
    for(unsigned i=0;i<(HB_SAMPLES+HB_WARMUP)*3;++i){uint64_t bytes;CHECK(cb_system_receive(input,sizeof(input),&bytes,0)==CB_OK);struct cb_delivery_header delivery;copy(&delivery,input,sizeof(delivery));CHECK(delivery.call.operation==1 && delivery.call.handle==cap && delivery.call.payload_bytes<=4096);
        struct cb_reply_header r={.version={sizeof(r),1,0},.request=delivery.call.request,.provider_epoch=delivery.provider_epoch,.outcome=CB_COMMITTED,.result_bytes=delivery.call.payload_bytes};copy(output,&r,sizeof(r));copy(output+sizeof(r),input+sizeof(delivery)+delivery.context_bytes,r.result_bytes);CHECK(cb_system_reply(output,sizeof(r)+r.result_bytes)==CB_OK);
    }cb_exit(0);
}
__attribute__((noreturn)) void _start(uint64_t role){
    if(role)server();
    const struct cb_interface_id memory=CB_MEMORY_INTERFACE_ID,io=CB_IO_INTERFACE_ID;cb_handle_t mem,echo;
    CHECK(cb_system_get_handle(CB_IO_NAME,sizeof(CB_IO_NAME)-1,&io,1,0,&clock_handle)==CB_OK);
    CHECK(cb_system_get_handle(CB_MEMORY_NAME,sizeof(CB_MEMORY_NAME)-1,&memory,1,0,&mem)==CB_OK && cb_system_get_handle("@bench",6,&interface,1,0,&echo)==CB_OK);
    for(unsigned kind=0;kind<HB_CASES;++kind)for(unsigned trial=0;trial<HB_SAMPLES+HB_WARMUP;++trial){uint64_t bytes,begin=clock_ns();
        if(kind==1){cb_handle_t acquired;CHECK(cb_system_get_handle(CB_MEMORY_NAME,sizeof(CB_MEMORY_NAME)-1,&memory,1,0,&acquired)==CB_OK);uint64_t end=clock_ns();CHECK(end>=begin && cb_system_release(acquired)==CB_OK);if(trial>=HB_WARMUP)report.ns[kind][trial-HB_WARMUP]=end-begin;continue;}
        if(kind==2){struct {struct cb_call_header h;struct cb_describe_request d;} r={.h=cb_call(mem,(struct cb_interface_id)CB_HANDLE_INTERFACE_ID,CB_HANDLE_DESCRIBE,sizeof(struct cb_describe_request)),.d={.version={sizeof(struct cb_describe_request),1,0},.interface=CB_MEMORY_INTERFACE_ID}};CHECK(cb_system_invoke(&r,sizeof(r),output,sizeof(output),&bytes)==CB_OK);forget();}
        if(kind==3)(void)clock_ns();
        if(kind>=4 && kind<=6){size_t n=kind==4?0:kind==5?128:4096;struct cb_call_header c=cb_call(echo,interface,1,n);copy(input,&c,sizeof(c));for(size_t i=0;i<n;++i)input[sizeof(c)+i]=(unsigned char)(i*37+trial);CHECK(cb_system_invoke(input,sizeof(c)+n,output,sizeof(output),&bytes)==CB_OK && bytes==sizeof(struct cb_reply_header)+n);for(size_t i=0;i<n;++i)CHECK(output[sizeof(struct cb_reply_header)+i]==input[sizeof(c)+i]);forget();}
        if(kind==7){struct {struct cb_call_header h;struct cb_memory_request m;} r={.h=cb_call(mem,memory,CB_MEMORY_ALLOCATE,sizeof(struct cb_memory_request)),.m={.bytes=4096}};CHECK(cb_system_invoke(&r,sizeof(r),output,sizeof(output),&bytes)==CB_OK);struct cb_memory_result m;copy(&m,output+sizeof(struct cb_reply_header),sizeof(m));forget();r.h=cb_call(mem,memory,CB_MEMORY_FREE,sizeof(r.m));r.m=(struct cb_memory_request){.address=m.address};CHECK(cb_system_invoke(&r,sizeof(r),output,sizeof(output),&bytes)==CB_OK);forget();}
        uint64_t end=clock_ns();CHECK(end>=begin);if(trial>=HB_WARMUP)report.ns[kind][trial-HB_WARMUP]=end-begin;
    }
    CHECK(cb_system_release(echo)==CB_OK && cb_system_release(mem)==CB_OK && cb_system_release(clock_handle)==CB_OK);report.magic=HB_MAGIC;report.cases=HB_CASES;report.samples=HB_SAMPLES;cb_exit((uintptr_t)&report);
}
