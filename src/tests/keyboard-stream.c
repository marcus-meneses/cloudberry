#include <assert.h>
#include <stdio.h>
#include <string.h>
#define _start keyboard_application_entry
#include "../handlers/keyboard/keyboard.c"
#undef _start

static unsigned char captured[CB_FRAME_LIMIT];
static size_t captured_bytes;
static unsigned replies;
cb_status_t cb_system_reply(const void *data,uint64_t bytes){assert(bytes<=sizeof(captured));memcpy(captured,data,bytes);captured_bytes=bytes;++replies;return CB_OK;}
cb_status_t cb_system_get_handle(const char *n,uint64_t bytes,const struct cb_interface_id *i,uint16_t major,uint16_t minor,cb_handle_t *out){(void)n;(void)bytes;(void)i;(void)major;(void)minor;(void)out;assert(0);return CB_UNAVAILABLE;}
cb_status_t cb_system_invoke(const void *a,uint64_t b,void *c,uint64_t d,uint64_t *e){(void)a;(void)b;(void)c;(void)d;(void)e;assert(0);return CB_UNAVAILABLE;}
cb_status_t cb_system_release(cb_handle_t cap){(void)cap;assert(0);return CB_UNAVAILABLE;}
cb_status_t cb_system_publish(const void *a,uint64_t b,cb_handle_t *c){(void)a;(void)b;(void)c;assert(0);return CB_UNAVAILABLE;}
cb_status_t cb_system_receive(void *a,uint64_t b,uint64_t *c,uint64_t d){(void)a;(void)b;(void)c;(void)d;assert(0);return CB_UNAVAILABLE;}
cb_status_t cb_system_request_release(struct cb_request_id id){(void)id;assert(0);return CB_UNAVAILABLE;}
__attribute__((noreturn)) void cb_exit(uint64_t status){(void)status;assert(0);__builtin_trap();}
static struct cb_reply_header header(void){struct cb_reply_header h;assert(captured_bytes>=sizeof(h));memcpy(&h,captured,sizeof(h));return h;}
static struct cb_keyboard_batch batch(void){struct cb_keyboard_batch b;assert(header().result_bytes>=sizeof(b));memcpy(&b,captured+sizeof(struct cb_reply_header),sizeof(b));return b;}
int main(void){
    static struct cb_keyboard_event fixture[8];history=fixture;capacity=8;count=3;head=0;epoch=7;sequence=3;emitted_ns=300;
    for(unsigned i=0;i<3;++i)fixture[i]=(struct cb_keyboard_event){.version={sizeof(*fixture),1,0},.sequence=i+1,.timestamp_ns=(i+1)*100,.kind=CB_KEY_TEXT,.codepoint=i?'a':0};
    struct pending request={.read={.version={sizeof(struct cb_keyboard_read),1,0},.max_events=1},.grant_ns=150};
    assert(read_request(&request,1) && replies==1 && header().status==CB_OK && batch().count==1 && batch().next_sequence==2);
    struct cb_keyboard_event event;memcpy(&event,captured+sizeof(struct cb_reply_header)+sizeof(struct cb_keyboard_batch),sizeof(event));assert(event.sequence==2);
    unsigned char previous[CB_FRAME_LIMIT];size_t previous_bytes=captured_bytes;memcpy(previous,captured,captured_bytes);
    assert(read_request(&request,1) && captured_bytes==previous_bytes && !memcmp(previous,captured,captured_bytes));
    request.read.provider_epoch=epoch;request.read.after_sequence=3;unsigned before=replies;assert(!read_request(&request,1) && replies==before);
    request.delivery.call.deadline_ns=300;assert(read_request(&request,1) && header().status==CB_TIMEOUT && !header().result_bytes);
    request.delivery.call.deadline_ns=0;request.read.provider_epoch=6;assert(read_request(&request,1) && header().status==CB_INPUT_RESYNC && batch().provider_epoch==7 && batch().next_sequence==3);
    request.read.provider_epoch=epoch;request.read.after_sequence=0;evicted_sequence=1;evicted_ns=100;assert(read_request(&request,1) && header().status==CB_INPUT_RESYNC);
    request.read.provider_epoch=0;request.grant_ns=150;assert(read_request(&request,1) && header().status==CB_OK && batch().next_sequence==2);
    request.grant_ns=100;assert(read_request(&request,1) && header().status==CB_INPUT_RESYNC);
    evicted_ns=evicted_sequence=0;request.grant_ns=0;assert(read_request(&request,1) && header().status==CB_OK);memcpy(&event,captured+sizeof(struct cb_reply_header)+sizeof(struct cb_keyboard_batch),sizeof(event));assert(event.kind==CB_KEY_TEXT && event.codepoint==0);
    emitted_ns=UINT64_C(5000000301);prune();assert(!count && evicted_sequence==3);assert(read_request(&request,1) && header().status==CB_INPUT_RESYNC);
    uint64_t grant=0;const char context[]="{\"input_stream\":\"boot:1\",\"grant_ns\":18446744073709551615,\"process\":1,\"installation\":\"00000000000000000000000000000000\"}";assert(admission((const void *)context,sizeof(context)-1,&grant) && grant==UINT64_MAX);
    const char overflow[]="{\"input_stream\":\"boot:1\",\"grant_ns\":18446744073709551616,\"process\":1,\"installation\":\"00000000000000000000000000000000\"}";assert(!admission((const void *)overflow,sizeof(overflow)-1,&grant));
    /* A timed-read node remains reusable without another memory-handle call.
     * The invoke stub above deliberately rejects any unexpected allocation. */
    struct pending recycled=request;spare=NULL;
    for(unsigned i=0;i<100;++i){pending=&recycled;recycled.next=NULL;serve_pending();assert(!pending && spare==&recycled);assert(pending_allocate()==&recycled && !spare);}
    puts("PASS keyboard stream: grant boundary, independent replay, blocking deadline, stale/evicted cursor anchors, retention, Unicode NUL and authenticated context framing");return 0;
}
