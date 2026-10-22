#include "../../sdk/libc.h"
#include "../../interfaces/identity.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <fenv.h>
#include <malloc.h>
#include <errno.h>
#include <unistd.h>
#include <sys/time.h>
#include <time.h>
#define CHECK(x) do{if(!(x)){printf("FAIL runtime foundation line %d\n",__LINE__);return 1;}}while(0)
static struct cb_client async;
static struct cb_runtime isolated;
static unsigned char data[12001],copy[12001];
static unsigned timer_calls,timer_order;
static void timer(void *p){++timer_calls;timer_order=timer_order*10+(unsigned)(uintptr_t)p;}
int main(int argc,char **argv){
    CHECK(argc>0 && argv[0]);struct cb_runtime *rt=cb_libc_runtime();
    if(argc==2 && !strcmp(argv[1],"deny")){struct cb_io_utc denied;unsigned char bytes[8];CHECK(cb_runtime_utc(rt,&denied)==CB_DENIED && cb_runtime_random(rt,bytes,sizeof(bytes))==CB_DENIED);for(size_t i=0;i<sizeof(bytes);++i)CHECK(!bytes[i]);return 74;}
    if(argc==2 && !strcmp(argv[1],"delay")){CHECK(cb_runtime_delay(rt,500000000)==CB_OK);return 73;}
    CHECK(argc==1);
    void *aligned=aligned_alloc(4096,8192);CHECK(aligned && !((uintptr_t)aligned&4095));free(aligned);
    cb_runtime_init(&isolated);void *owned=cb_heap_alloc(&isolated.heap,1);CHECK(owned && cb_runtime_destroy(&isolated)==CB_BUSY && cb_heap_free(&isolated.heap,owned) && cb_runtime_destroy(&isolated)==CB_OK && !isolated.heap.mapped && !isolated.memory);
    void *small[1000];for(unsigned i=0;i<1000;++i){small[i]=malloc(19);CHECK(small[i] && !((uintptr_t)small[i]&15));memset(small[i],i,19);}for(unsigned i=0;i<1000;++i)free(small[i]);
    unsigned char *p=calloc(91,3);CHECK(p);for(unsigned i=0;i<273;++i)CHECK(!p[i]);memset(p,42,273);unsigned char *grown=realloc(p,90000);CHECK(grown);for(unsigned i=0;i<273;++i)CHECK(grown[i]==42);CHECK(malloc_usable_size(grown)>=90000);volatile size_t too_big=SIZE_MAX;CHECK(!calloc(too_big,2));CHECK(!realloc(grown,too_big));for(unsigned i=0;i<273;++i)CHECK(grown[i]==42);free(grown);
    volatile double two=2.0,minus=-1.0;CHECK(sqrt(two*two)==2.0 && pow(two,10)==1024 && isnan(sqrt(minus)) && isinf(1.0/(two-two)) && signbit(-0.0));
    CHECK(fabs(sin(0.5)-0.479425538604203)<1e-15 && fabs(log(exp(1.0))-1.0)<1e-15 && atan2(-0.0,-1.0)<-3.14);
    CHECK(fesetround(FE_DOWNWARD)==0 && nearbyint(1.9)==1.0);CHECK(fesetround(FE_TONEAREST)==0);
    char formatted[80];CHECK(snprintf(formatted,sizeof(formatted),"%.3f/%lld/%s",1.25,42LL,"case")==13 && !strcmp(formatted,"1.250/42/case"));CHECK(strtod("1.25",NULL)==1.25);
    struct timeval tv;CHECK(gettimeofday(&tv,NULL)==0 && tv.tv_sec>1577836800 && tv.tv_usec==0);time_t stamp=tv.tv_sec;struct tm utc;CHECK(gmtime_r(&stamp,&utc) && utc.tm_year>=120);
    uint64_t before,after;CHECK(cb_runtime_now(rt,&before)==CB_OK && cb_runtime_delay(rt,10000000)==CB_OK && cb_runtime_now(rt,&after)==CB_OK && after>=before+10000000);
    uint64_t timer1,timer2,canceled,timer_ready;CHECK(cb_runtime_timer(rt,after+1000000,timer,(void *)1,&timer1)==CB_OK && cb_runtime_timer(rt,after+1000000,timer,(void *)2,&timer2)==CB_OK && cb_runtime_timer(rt,after+1000000,timer,(void *)9,&canceled)==CB_OK && cb_runtime_cancel_timer(rt,canceled)==CB_OK && cb_runtime_cancel_timer(rt,canceled)==CB_BAD_REQUEST);
    CHECK(cb_runtime_poll(rt,NULL,0,after+100000000,1,&timer_ready)==CB_OK && timer_ready==UINT64_MAX && timer_calls==1 && timer_order==1);CHECK(cb_runtime_poll(rt,NULL,0,after+100000000,1,&timer_ready)==CB_OK && timer_calls==2 && timer_order==12 && !rt->timers);
    unsigned char random[130];cb_status_t entropy=cb_runtime_random(rt,random,sizeof(random));CHECK(entropy==CB_OK || entropy==CB_UNAVAILABLE);if(entropy)for(size_t i=0;i<sizeof(random);++i)CHECK(!random[i]);
    cb_handle_t denied=0;CHECK(cb_system_get_handle("/@identity",10,&(struct cb_interface_id)CB_IDENTITY_INTERFACE_ID,1,0,&denied)==CB_DENIED);
    for(size_t i=0;i<sizeof(data);++i)data[i]=(unsigned char)(i*37+11);
    FILE *file=fopen("/runtime-foundation.bin","wb");CHECK(file && fwrite(data,1,sizeof(data),file)==sizeof(data) && fclose(file)==0);
    file=fopen("/runtime-foundation.bin","rb");CHECK(file && fread(copy,1,sizeof(copy),file)==sizeof(copy) && !memcmp(data,copy,sizeof(data)));CHECK(fgetc(file)==EOF && feof(file) && fseek(file,0,SEEK_SET)!=0 && errno==ESPIPE);CHECK(fclose(file)==0);
    unsigned char *loaded;size_t size;CHECK(cb_runtime_load(rt,"/system/..","runtime-foundation.bin",&loaded,&size)==CB_OK && size==sizeof(data) && loaded[size]==0 && !memcmp(data,loaded,size));CHECK(cb_heap_free(&rt->heap,loaded));
    file=fopen("/runtime-foundation.bin","ab");CHECK(file && fwrite("tail",1,4,file)==4 && fclose(file)==0);CHECK(cb_runtime_load(rt,"/","runtime-foundation.bin",&loaded,&size)==CB_OK && size==sizeof(data)+4 && !memcmp(data,loaded,sizeof(data)) && !memcmp(loaded+sizeof(data),"tail",4));CHECK(cb_heap_free(&rt->heap,loaded));
    cb_handle_t streams=cb_acquire(CB_STREAM_NAME,(struct cb_interface_id)CB_STREAM_INTERFACE_ID);CHECK(cb_invoke(&async,streams,(struct cb_interface_id)CB_STREAM_INTERFACE_ID,CB_STREAM_PIPE,NULL,0,0)==CB_OK);struct cb_stream_pair pipe;cb_copy(&pipe,cb_result(&async),sizeof(pipe));
    size_t actual;CHECK(cb_runtime_read(rt,pipe.reader,copy,sizeof(copy),&actual,CB_STREAM_NONBLOCK)==CB_WOULD_BLOCK && !actual);
    CHECK(cb_runtime_write_all(rt,pipe.writer,data,sizeof(data))==CB_OK);struct cb_wait_item item={.kind=CB_WAIT_READ,.handle=pipe.reader};uint64_t ready;CHECK(cb_system_wait(&item,1,0,&ready)==CB_OK && ready==0);
    size_t used=0;while(used<sizeof(copy)){CHECK(cb_runtime_read(rt,pipe.reader,copy+used,sizeof(copy)-used,&actual,0)==CB_OK && actual);used+=actual;}CHECK(!memcmp(data,copy,sizeof(data)));CHECK(cb_runtime_close(rt,pipe.writer)==CB_OK && cb_runtime_read(rt,pipe.reader,copy,1,&actual,0)==CB_OK && !actual);CHECK(cb_runtime_close(rt,pipe.reader)==CB_OK && cb_runtime_close(rt,streams)==CB_OK);
    const char *args[]={"true"};uint64_t pid;CHECK(cb_runtime_launch(rt,"true",1,args,"/",NULL,&pid)==CB_OK);struct cb_application_ref ref={pid};CHECK(cb_runtime_submit(rt,&async,rt->applications,(struct cb_interface_id)CB_APPLICATIONS_INTERFACE_ID,CB_APPLICATION_WAIT,&ref,sizeof(ref))==CB_OK);
    struct cb_wait_item request={.kind=CB_WAIT_REQUEST,.request=async.pending};CHECK(cb_runtime_now(rt,&before)==CB_OK && cb_system_wait(&request,1,before+5000000000,&ready)==CB_OK && cb_runtime_collect(rt,&async)==CB_OK);struct cb_application_status child;cb_copy(&child,cb_result(&async),sizeof(child));CHECK(child.process==pid && child.state==CB_APPLICATION_EXITED && !child.exit_code);CHECK(cb_runtime_child(rt,CB_APPLICATION_RELEASE,pid,0,NULL)==CB_OK);
    CHECK(cb_runtime_child(rt,CB_APPLICATION_INFO,UINT64_MAX,0,&child)==CB_DENIED && rt->failure==CB_DENIED && rt->failure_bytes>=sizeof(struct cb_reply_header));
    const char *restricted[]={"runtime-restricted","deny"};CHECK(cb_runtime_launch(rt,"runtime-restricted",2,restricted,"/",NULL,&pid)==CB_OK && cb_runtime_child(rt,CB_APPLICATION_WAIT,pid,0,&child)==CB_OK && child.exit_code==74 && cb_runtime_child(rt,CB_APPLICATION_RELEASE,pid,0,NULL)==CB_OK);
    const char *delayed[]={"runtime-foundation","delay"};CHECK(cb_runtime_launch(rt,"runtime-foundation",2,delayed,"/",NULL,&pid)==CB_OK);ref.process=pid;CHECK(cb_runtime_submit(rt,&async,rt->applications,(struct cb_interface_id)CB_APPLICATIONS_INTERFACE_ID,CB_APPLICATION_WAIT,&ref,sizeof(ref))==CB_OK);struct cb_abandon_result abandoned;CHECK(cb_abandon(&async,&abandoned)==CB_OK && !async.pending.serial);CHECK(cb_runtime_child(rt,CB_APPLICATION_WAIT,pid,0,&child)==CB_OK && child.exit_code==73 && cb_runtime_child(rt,CB_APPLICATION_RELEASE,pid,0,NULL)==CB_OK);
    printf("PASS runtime foundations: allocator C/math UTC streams files deadlines async child ownership\n");return 0;
}
