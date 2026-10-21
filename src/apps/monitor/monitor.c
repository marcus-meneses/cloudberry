#include "../../sdk/application.h"
#include "../../sdk/stream.h"
#include "../../interfaces/text.h"
#include "../../interfaces/io.h"
#include "model.h"
#include "../../interfaces/keyboard.h"
struct row {struct cb_process_sample sample;int cpu;unsigned seen;};
static struct cb_client client,keyboard_client;
static cb_handle_t applications,memory,clock_handle,text,input,output,keyboard;
static unsigned owns_input;
static struct cb_keyboard_read_focus keyread={.base={.version={sizeof(struct cb_keyboard_read_focus),1,1},.max_events=32}};
static struct row *rows;static size_t count,capacity,page;
static uint32_t columns=120,height=0,cpus;
static char *screen,*previous;static unsigned live;
static __attribute__((noreturn)) void failure(unsigned line){
    static struct cb_client diagnostic;char message[64]="monitor: operation failed at line ";size_t n=cb_length(message);char digits[10];unsigned used=0;
    do{digits[used++]='0'+line%10;line/=10;}while(line);while(used)message[n++]=digits[--used];message[n++]='\n';
    cb_handle_t error=cb_standard(2);if(error)(void)cb_write_all(&diagnostic,error,message,n);cb_exit(CB_RESOURCE_FAILURE);
}
#undef CB_REQUIRE
#define CB_REQUIRE(x) do {if(!(x))failure(__LINE__);}while(0)
static cb_handle_t acquire(const char *name,struct cb_interface_id id){cb_handle_t handle=0;CB_REQUIRE(cb_system_get_handle(name,cb_length(name),&id,1,0,&handle)==CB_OK);return handle;}
static void *allocate(size_t bytes){struct cb_memory_request r={.bytes=bytes};CB_REQUIRE(cb_invoke(&client,memory,(struct cb_interface_id)CB_MEMORY_INTERFACE_ID,CB_MEMORY_ALLOCATE,&r,sizeof(r),0)==CB_OK);struct cb_memory_result result;cb_copy(&result,cb_result(&client),sizeof(result));return (void *)(uintptr_t)result.address;}
static void release(void *p){if(!p)return;struct cb_memory_request r={.address=(uintptr_t)p};CB_REQUIRE(cb_invoke(&client,memory,(struct cb_interface_id)CB_MEMORY_INTERFACE_ID,CB_MEMORY_FREE,&r,sizeof(r),0)==CB_OK);}
static uint64_t now(void){CB_REQUIRE(cb_invoke(&client,clock_handle,(struct cb_interface_id)CB_IO_INTERFACE_ID,CB_IO_NOW_NS,NULL,0,0)==CB_OK);uint64_t value;cb_copy(&value,cb_result(&client),sizeof(value));return value;}
static void sweep(void){
    for(size_t i=0;i<count;++i)rows[i].seen=0;
    struct cb_process_query query={0};
    for(;;){CB_REQUIRE(cb_invoke(&client,applications,(struct cb_interface_id)CB_APPLICATIONS_INTERFACE_ID,CB_APPLICATION_PROCESSES,&query,sizeof(query),0)==CB_OK);struct cb_reply_header reply;cb_copy(&reply,client.result,sizeof(reply));if(!reply.result_bytes)break;CB_REQUIRE(reply.result_bytes==sizeof(struct cb_process_sample));struct cb_process_sample sample;cb_copy(&sample,cb_result(&client),sizeof(sample));CB_REQUIRE(sample.process>query.after && sample.process<=sample.ceiling && sample.name_bytes<=sizeof(sample.name));query.after=sample.process;query.ceiling=sample.ceiling;cpus=sample.cpus;
        size_t at=0;while(at<count && rows[at].sample.process!=sample.process)++at;
        if(at==count){if(count==capacity){size_t next=capacity?capacity*2:16;CB_REQUIRE(next>capacity && next<=SIZE_MAX/sizeof(*rows));struct row *larger=allocate(next*sizeof(*rows));cb_copy(larger,rows,count*sizeof(*rows));release(rows);rows=larger;capacity=next;}rows[count++]=(struct row){.cpu=-1};}
        else rows[at].cpu=monitor_cpu(&rows[at].sample,&sample);
        rows[at].sample=sample;rows[at].seen=1;
    }
    for(size_t i=0;i<count;)if(!rows[i].seen)rows[i]=rows[--count];else ++i;
    for(size_t i=1;i<count;++i){struct row item=rows[i];size_t j=i;while(j && (rows[j-1].cpu<item.cpu || (rows[j-1].cpu==item.cpu && rows[j-1].sample.process>item.sample.process))){rows[j]=rows[j-1];--j;}rows[j]=item;}
}
struct line {char *text;size_t used,capacity;};
static void character(struct line *l,char c){if(l->used<l->capacity)l->text[l->used++]=c;}
static void word(struct line *l,const char *s){while(*s)character(l,*s++);}
static void number(struct line *l,uint64_t n){char digits[20];unsigned count=0;do{digits[count++]='0'+n%10;n/=10;}while(n);while(count)character(l,digits[--count]);}
static void pad(struct line *l,size_t target){while(l->used<target && l->used<l->capacity)character(l,' ');}
static void row_line(struct line *l,const struct row *r){
    number(l,r->sample.process);pad(l,9);number(l,r->sample.cpu);pad(l,15);word(l,monitor_state(&r->sample));pad(l,24);
    if(r->cpu<0)word(l,"--");else {number(l,(unsigned)r->cpu/10);character(l,'.');character(l,'0'+r->cpu%10);}pad(l,33);number(l,r->sample.user_bytes/1024);pad(l,46);number(l,r->sample.cpu_ns/1000000000);character(l,'.');unsigned ms=r->sample.cpu_ns/1000000%1000;character(l,'0'+ms/100);character(l,'0'+ms/10%10);character(l,'0'+ms%10);pad(l,59);
    for(unsigned i=0;i<r->sample.name_bytes;++i){unsigned char c=r->sample.name[i];character(l,c>=32 && c<127?c:'?');}
}
static void cursor(uint32_t row){struct cb_text_cursor c={0,row};CB_REQUIRE(cb_invoke(&client,text,(struct cb_interface_id)CB_TEXT_INTERFACE_ID,CB_TEXT_CURSOR,&c,sizeof(c),0)==CB_OK);}
static void display(void){
    if(!live){char buffer[1024];struct line l={buffer,0,sizeof(buffer)};word(&l,"Cloudberry monitor: processes ");number(&l,count);word(&l,"  CPUs ");number(&l,cpus);character(&l,'\n');word(&l,"PID      CPU   STATE    CPU%     USER KiB     CPU TIME(s)  NAME\n");CB_REQUIRE(cb_write_all(&client,output,buffer,l.used)==CB_OK);for(size_t i=0;i<count;++i){l.used=0;row_line(&l,&rows[i]);character(&l,'\n');CB_REQUIRE(cb_write_all(&client,output,buffer,l.used)==CB_OK);}return;}
    size_t width=columns-1,total=width*(height-1);for(size_t i=0;i<total;++i)screen[i]=' ';
    struct line l={screen,0,width};word(&l,"Cloudberry monitor | processes ");number(&l,count);word(&l," | CPUs ");number(&l,cpus);word(&l," | Esc: quit; Up/Down: page");
    l=(struct line){screen+width,0,width};word(&l,"PID      CPU   STATE    CPU%     USER KiB     CPU TIME(s)  NAME");size_t visible=height-4,pages=count?(count+visible-1)/visible:1;if(page>=pages)page=pages-1;
    for(size_t n=0;n<visible && page*visible+n<count;++n){l=(struct line){screen+(n+2)*width,0,width};row_line(&l,&rows[page*visible+n]);}
    l=(struct line){screen+(height-2)*width,0,width};word(&l,"Page ");number(&l,page+1);character(&l,'/');number(&l,pages);word(&l," | CPU% uses consecutive samples; USER KiB = committed user pages");
    for(uint32_t row=0;row<height-1;++row){size_t start=(size_t)row*width;int changed=0;for(size_t x=0;x<width;++x)if(screen[start+x]!=previous[start+x]){changed=1;break;}if(changed){cursor(row);for(size_t offset=0;offset<width;){size_t bytes=width-offset,chunk=CB_FRAME_LIMIT-sizeof(struct cb_call_header)-512;if(bytes>chunk)bytes=chunk;CB_REQUIRE(cb_invoke(&client,text,(struct cb_interface_id)CB_TEXT_INTERFACE_ID,CB_TEXT_WRITE,screen+start+offset,bytes,0)==CB_OK);offset+=bytes;}cb_copy(previous+start,screen+start,width);}}
    cursor(height-1);
}
static void request_keyboard(void){if(!keyboard_client.pending.serial)CB_REQUIRE(cb_submit(&keyboard_client,keyboard,(struct cb_interface_id)CB_KEYBOARD_INTERFACE_ID,CB_KEYBOARD_READ,&keyread,sizeof(keyread))==CB_OK);}
static int keyboard_events(void){
    request_keyboard();cb_status_t status=cb_collect(&keyboard_client);if(status==CB_WOULD_BLOCK && !keyboard_client.collected)return 0;
    CB_REQUIRE(status==CB_OK || status==CB_INPUT_RESYNC);struct cb_keyboard_batch_focus batch;CB_REQUIRE(keyboard_client.bytes>=sizeof(struct cb_reply_header)+sizeof(batch));cb_copy(&batch,cb_result(&keyboard_client),sizeof(batch));
    CB_REQUIRE(batch.base.version.bytes==sizeof(batch) && batch.base.version.major==1 && batch.base.version.minor==1 && batch.base.count<=32 && sizeof(batch)+(uint64_t)batch.base.count*sizeof(struct cb_keyboard_event)<=keyboard_client.bytes-sizeof(struct cb_reply_header));
    keyread.base.provider_epoch=batch.base.provider_epoch;keyread.base.after_sequence=batch.base.next_sequence;keyread.focus_generation=batch.focus_generation;
    if(status==CB_INPUT_RESYNC){request_keyboard();return 0;}
    int quit=0;for(unsigned i=0;i<batch.base.count;++i){struct cb_keyboard_event key;cb_copy(&key,(unsigned char *)cb_result(&keyboard_client)+sizeof(batch)+i*sizeof(key),sizeof(key));CB_REQUIRE(cb_keyboard_event_valid(&key));
        if((key.kind!=CB_KEY_DOWN && key.kind!=CB_KEY_REPEAT) || key.usage_page!=7)continue;
        if(key.usage==41){quit=1;break;}
        size_t next=monitor_page(page,count,height,key.usage);if(next!=page){page=next;display();}
    }
    if(!quit)request_keyboard();
    return quit;
}
static uint64_t integer(const char *s){CB_REQUIRE(s && *s);uint64_t n=0;while(*s){CB_REQUIRE(*s>='0' && *s<='9' && n<=(UINT64_MAX-(*s-'0'))/10);n=n*10+*s++-'0';}CB_REQUIRE(n);return n;}
__attribute__((noreturn)) void _start(const struct cb_startup *packet){
    struct cb_application_start start;CB_REQUIRE(cb_application_start(packet,&start));uint64_t limit=0,interval=1000000000;int batch=0;
    for(uint64_t i=1;i<start.header.argc;++i){const char *a=cb_application_argument(&start,i);if(a[0]=='-' && a[1]=='b' && !a[2])batch=1;else if(a[0]=='-' && a[1]=='n' && !a[2]){limit=integer(cb_application_argument(&start,++i));batch=1;}else if(a[0]=='-' && a[1]=='d' && !a[2]){uint64_t seconds=integer(cb_application_argument(&start,++i));CB_REQUIRE(seconds<=UINT64_MAX/1000000000);interval=seconds*1000000000;}else cb_exit(CB_BAD_REQUEST);}
    applications=acquire(CB_APPLICATIONS_NAME,(struct cb_interface_id)CB_APPLICATIONS_INTERFACE_ID);memory=acquire(CB_MEMORY_NAME,(struct cb_interface_id)CB_MEMORY_INTERFACE_ID);clock_handle=acquire(CB_IO_NAME,(struct cb_interface_id)CB_IO_INTERFACE_ID);input=cb_standard(0);output=cb_standard(1);CB_REQUIRE(output);
    if(!batch){text=acquire(CB_TEXT_NAME,(struct cb_interface_id)CB_TEXT_INTERFACE_ID);CB_REQUIRE(cb_invoke(&client,text,(struct cb_interface_id)CB_TEXT_INTERFACE_ID,CB_TEXT_INFO,NULL,0,0)==CB_OK);struct cb_text_info info;cb_copy(&info,cb_result(&client),sizeof(info));columns=info.columns;height=info.rows;if(columns>1 && height>=5){size_t width=columns-1;CB_REQUIRE(height-1<=SIZE_MAX/width && width*(height-1)<=SIZE_MAX/2);size_t bytes=width*(height-1);screen=allocate(bytes);previous=allocate(bytes);for(size_t i=0;i<bytes;++i)previous[i]=0;live=1;}}
    if(live){keyboard=acquire(CB_KEYBOARD_NAME,(struct cb_interface_id)CB_KEYBOARD_INTERFACE_ID);CB_REQUIRE(cb_invoke(&client,keyboard,(struct cb_interface_id)CB_KEYBOARD_INTERFACE_ID,CB_KEYBOARD_FOCUS_CLAIM,NULL,0,0)==CB_OK);owns_input=1;struct cb_keyboard_focus focus;cb_copy(&focus,cb_result(&client),sizeof(focus));keyread.base.provider_epoch=focus.provider_epoch;keyread.base.after_sequence=focus.next_sequence;keyread.focus_generation=focus.generation;request_keyboard();}
    uint64_t samples=0;int quit=0;
    while(!quit){sweep();display();if(limit && ++samples>=limit)break;uint64_t time=now();CB_REQUIRE(time<=UINT64_MAX-interval);uint64_t deadline=time+interval;
        for(;;){if(live){quit=keyboard_events();if(quit)break;}else if(input){unsigned char data[128];size_t n=0;cb_status_t status=cb_stream_read(&client,input,data,sizeof(data),&n,CB_STREAM_NONBLOCK);if(status==CB_OK && !n){quit=1;break;}CB_REQUIRE(status==CB_OK || status==CB_WOULD_BLOCK);}
            if(now()>=deadline)break;
            struct cb_wait_item item=live?(struct cb_wait_item){.kind=CB_WAIT_REQUEST,.request=keyboard_client.pending}:(struct cb_wait_item){.kind=CB_WAIT_READ,.handle=input};uint64_t ready;cb_status_t status=cb_system_wait(live || input?&item:NULL,live || input?1:0,deadline,&ready);CB_REQUIRE(status==CB_OK || status==CB_TIMEOUT);
        }
    }
    if(keyboard_client.pending.serial){struct cb_abandon_result abandoned;CB_REQUIRE(cb_abandon(&keyboard_client,&abandoned)==CB_OK);}
    if(live)CB_REQUIRE(cb_invoke(&client,text,(struct cb_interface_id)CB_TEXT_INTERFACE_ID,CB_TEXT_CLEAR,NULL,0,0)==CB_OK);
    if(owns_input){CB_REQUIRE(cb_invoke(&client,keyboard,(struct cb_interface_id)CB_KEYBOARD_INTERFACE_ID,CB_KEYBOARD_FOCUS_RELEASE,NULL,0,0)==CB_OK);owns_input=0;}
    release(screen);release(previous);release(rows);cb_exit(0);
}
