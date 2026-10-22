#include "../../sdk/application.h"
#include "../../sdk/stream.h"
#include "../../interfaces/text.h"
static struct cb_client client;
static cb_handle_t memory;
struct item {uint16_t bytes;char name[256];};
static void release(void *pointer){struct cb_memory_request request={.address=(uintptr_t)pointer};CB_REQUIRE(cb_invoke(&client,memory,(struct cb_interface_id)CB_MEMORY_INTERFACE_ID,CB_MEMORY_FREE,&request,sizeof(request),0)==CB_OK);}
static void *grow(void *pointer,size_t old_bytes,size_t bytes){
    struct cb_memory_request request={.bytes=bytes};
    CB_REQUIRE(cb_invoke(&client,memory,(struct cb_interface_id)CB_MEMORY_INTERFACE_ID,CB_MEMORY_ALLOCATE,&request,sizeof(request),0)==CB_OK);
    struct cb_memory_result result;cb_copy(&result,cb_result(&client),sizeof(result));void *next=(void *)(uintptr_t)result.address;
    if(pointer){cb_copy(next,pointer,old_bytes);release(pointer);}return next;
}
__attribute__((noreturn)) void _start(const struct cb_startup *packet){
    struct cb_application_start start;CB_REQUIRE(cb_application_start(packet,&start) && start.header.cwd_bytes<4096 && start.header.argc<=3);
    char cwd[4096],path[4096];cb_copy(cwd,start.cwd,start.header.cwd_bytes);cwd[start.header.cwd_bytes]=0;
    int all=0,selected=0;const char *argument=".";
    for(uint64_t i=1;i<start.header.argc;++i){const char *a=cb_application_argument(&start,i);if(a[0]=='-' && a[1]=='a' && !a[2])all=1;else {CB_REQUIRE(!selected);argument=a;selected=1;}}
    CB_REQUIRE(cb_path(path,sizeof(path),cwd,argument));
    cb_handle_t fs=cb_acquire(CB_FILESYSTEM_NAME,(struct cb_interface_id)CB_FILESYSTEM_INTERFACE_ID),out=cb_standard(1);if(!out)cb_exit(CB_BAD_HANDLE);
    memory=cb_acquire(CB_MEMORY_NAME,(struct cb_interface_id)CB_MEMORY_INTERFACE_ID);
    unsigned width=80;cb_handle_t text=0;const struct cb_interface_id text_id=CB_TEXT_INTERFACE_ID;
    if(cb_system_get_handle(CB_TEXT_NAME,sizeof(CB_TEXT_NAME)-1,&text_id,1,0,&text)==CB_OK){
        if(cb_invoke(&client,text,text_id,CB_TEXT_INFO,NULL,0,0)==CB_OK){struct cb_text_info info;cb_copy(&info,cb_result(&client),sizeof(info));if(info.columns>1)width=info.columns;}
        CB_REQUIRE(cb_system_release(text)==CB_OK);
    }
    uint64_t cursor=0;struct item *items=NULL;size_t count=0,capacity=0,longest=0;
    for(;;){
        cb_status_t status=cb_fs_call(&client,fs,CB_FILESYSTEM_LIST,path,0,cursor);
        if(status){cb_handle_t error=cb_standard(2);static const char message[]="ls: cannot list directory\n";if(error)(void)cb_write_all(&client,error,message,sizeof(message)-1);cb_exit(status);}
        struct cb_reply_header h;cb_copy(&h,client.result,sizeof(h));if(!h.result_bytes)break;
        struct cb_filesystem_entry e;cb_copy(&e,cb_result(&client),sizeof(e));if(e.cursor<=cursor || e.name_bytes>255)cb_exit(CB_BAD_REQUEST);cursor=e.cursor;
        if(!all && e.name_bytes && e.name[0]=='_')continue;
        if((e.name_bytes==1 && e.name[0]=='.') || (e.name_bytes==2 && e.name[0]=='.' && e.name[1]=='.'))continue;
        if(count==capacity){CB_REQUIRE(capacity<=SIZE_MAX/2/sizeof(*items));capacity=capacity?capacity*2:32;items=grow(items,count*sizeof(*items),capacity*sizeof(*items));}
        struct item *item=&items[count++];item->bytes=e.name_bytes;cb_copy(item->name,e.name,e.name_bytes);if(e.directory)item->name[item->bytes++]='/';
        if(item->bytes>longest)longest=item->bytes;
    }
    /* Leave the final screen cell unused to avoid implicit wrap plus newline. */
    unsigned columns=4;size_t cell=longest+2;
    if(cell*4>width-1)columns=3;
    while(columns>1 && cell*columns>width-1)--columns;
    cell=(width-1)/columns;
    for(size_t i=0;i<count;++i){
        CB_REQUIRE(cb_write_all(&client,out,items[i].name,items[i].bytes)==CB_OK);
        if((i+1)%columns==0 || i+1==count)CB_REQUIRE(cb_write_all(&client,out,"\n",1)==CB_OK);
        else {static const char spaces[]="                                                                ";size_t padding=cell-items[i].bytes;while(padding){size_t n=padding>sizeof(spaces)-1?sizeof(spaces)-1:padding;CB_REQUIRE(cb_write_all(&client,out,spaces,n)==CB_OK);padding-=n;}}
    }
    if(items)release(items);
    cb_exit(0);
}
