#include "../../sdk/stream.h"
#include "../../interfaces/io.h"
static struct cb_client client;
static const struct cb_interface_id test_id={{0x43,0x42,0x45,0x58,0x45,0x43,0x54,0x45,0x53,0x54,1}};
static uint64_t now(void){cb_handle_t io=cb_acquire(CB_IO_NAME,(struct cb_interface_id)CB_IO_INTERFACE_ID);CB_REQUIRE(cb_invoke(&client,io,(struct cb_interface_id)CB_IO_INTERFACE_ID,CB_IO_NOW_NS,NULL,0,0)==CB_OK);uint64_t value;cb_copy(&value,cb_result(&client),8);CB_REQUIRE(cb_system_release(io)==CB_OK);return value;}
__attribute__((noreturn)) void _start(const uint64_t *mode){
    if(!*mode){uint64_t expected=mode[1]^UINT64_C(0x123456789abcdef0);volatile uint64_t guard=expected;__asm__ volatile("movq %0,%%xmm15"::"r"(expected));for(;;){uint64_t vector;__asm__ volatile("movq %%xmm15,%0":"=r"(vector));CB_REQUIRE(guard==(mode[1]^UINT64_C(0x123456789abcdef0)) && vector==guard);__asm__ volatile("pause");}}
    if(*mode==1){cb_handle_t streams=cb_acquire(CB_STREAM_NAME,(struct cb_interface_id)CB_STREAM_INTERFACE_ID);CB_REQUIRE(cb_invoke(&client,streams,(struct cb_interface_id)CB_STREAM_INTERFACE_ID,CB_STREAM_PIPE,NULL,0,0)==CB_OK);struct cb_stream_pair pipe;cb_copy(&pipe,cb_result(&client),sizeof(pipe));unsigned char data;size_t bytes;CB_REQUIRE(cb_stream_read(&client,pipe.reader,&data,1,&bytes,0)==CB_OK);cb_exit(99);}
    if(*mode==2){cb_handle_t target=cb_acquire("@execution-test",test_id);uint64_t payload=42;(void)cb_invoke(&client,target,test_id,1,&payload,8,0);cb_exit(98);}
    CB_REQUIRE(*mode==3);const char *names[]={"test"};cb_handle_t target=cb_publish(&client,"@execution-test",test_id,names,1);unsigned char input[CB_FRAME_LIMIT];uint64_t bytes;
    CB_REQUIRE(cb_system_receive(input,sizeof(input),&bytes,0)==CB_OK && bytes>=sizeof(struct cb_delivery_header));struct cb_delivery_header delivery;cb_copy(&delivery,input,sizeof(delivery));
    uint64_t ready;CB_REQUIRE(cb_system_wait(NULL,0,now()+UINT64_C(2000000000),&ready)==CB_TIMEOUT);
    /* A terminated caller's copied request must disappear without dangling refs. */
    CB_REQUIRE(cb_respond(&client,&delivery,CB_OK,NULL,0)==CB_BAD_REQUEST);
    CB_REQUIRE(cb_system_release(target)==CB_OK);cb_exit(0);
}
