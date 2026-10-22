#include "../../sdk/application.h"
#ifndef CB_SAMPLE_REVISION
#define CB_SAMPLE_REVISION 1
#endif
#include "../../sdk/stream.h"
static struct cb_client client,console;
static cb_handle_t output;
static void say(const char *s){CB_REQUIRE(cb_write_all(&console,output,s,cb_length(s))==CB_OK);}
static void number(uint64_t n){char a[24],b[24];unsigned used=0;do{a[used++]='0'+n%10;n/=10;}while(n);for(unsigned i=0;i<used;++i)b[i]=a[used-i-1];CB_REQUIRE(cb_write_all(&console,output,b,used)==CB_OK);}
static int equal(const char *a,const char *b){while(*a && *a==*b){++a;++b;}return *a==*b;}
static void own_path(cb_handle_t apps,unsigned op,char path[4096],const char *tail){CB_REQUIRE(cb_invoke(&client,apps,(struct cb_interface_id)CB_APPLICATIONS_INTERFACE_ID,op,NULL,0,0)==CB_OK);struct cb_reply_header h;cb_copy(&h,client.result,sizeof(h));size_t n=cb_length(tail);CB_REQUIRE(h.result_bytes+n<4096);cb_copy(path,cb_result(&client),h.result_bytes);cb_copy(path+h.result_bytes,tail,n+1);}
__attribute__((noreturn)) void _start(const struct cb_startup *packet){struct cb_application_start start;CB_REQUIRE(cb_application_start(packet,&start));output=cb_standard(1);if(CB_SAMPLE_REVISION==2)say("sample revision 2\n");if(start.header.argc==2 && (equal(cb_application_argument(&start,1),"stat-check") || equal(cb_application_argument(&start,1),"mkdir-check"))){
    int stat=equal(cb_application_argument(&start,1),"stat-check");
    cb_handle_t apps=cb_acquire(CB_APPLICATIONS_NAME,(struct cb_interface_id)CB_APPLICATIONS_INTERFACE_ID),fs=cb_acquire(CB_FILESYSTEM_NAME,(struct cb_interface_id)CB_FILESYSTEM_INTERFACE_ID);char path[4096];own_path(apps,CB_APPLICATION_DATA,path,"/permission-probe");
    cb_status_t status=cb_fs_call(&client,fs,stat?CB_FILESYSTEM_STAT:CB_FILESYSTEM_MKDIR,path,0,0);say(stat?"stat status ":"mkdir status ");number(status);say("\n");cb_exit(0);
    }if(start.header.argc==2){unsigned char byte;size_t n;for(;;){CB_REQUIRE(cb_stream_read(&client,cb_standard(0),&byte,1,&n,0)==CB_OK);if(!n)cb_exit(0);}}
    cb_handle_t apps=cb_acquire(CB_APPLICATIONS_NAME,(struct cb_interface_id)CB_APPLICATIONS_INTERFACE_ID),fs=cb_acquire(CB_FILESYSTEM_NAME,(struct cb_interface_id)CB_FILESYSTEM_INTERFACE_ID);char path[4096];own_path(apps,CB_APPLICATION_DATA,path,"/state");cb_status_t s=cb_fs_call(&client,fs,CB_FILESYSTEM_STAT,path,0,0);uint64_t before=0;if(!s){struct cb_filesystem_stat st;cb_copy(&st,cb_result(&client),sizeof(st));before=st.bytes;}else CB_REQUIRE(s==CB_NOT_FOUND);CB_REQUIRE(cb_fs_call(&client,fs,CB_FILESYSTEM_OPEN,path,CB_FILE_APPEND,0)==CB_OK);cb_handle_t file;cb_copy(&file,cb_result(&client),8);CB_REQUIRE(cb_write_all(&client,file,"sample-state\n",13)==CB_OK && cb_system_release(file)==CB_OK);say("sample data ");number(before);say("\n");
    own_path(apps,CB_APPLICATION_RESOURCES,path,"/sub/hello.txt");CB_REQUIRE(cb_fs_call(&client,fs,CB_FILESYSTEM_OPEN,path,CB_FILE_READ,0)==CB_OK);cb_copy(&file,cb_result(&client),8);char data[128];size_t n;CB_REQUIRE(cb_stream_read(&client,file,data,sizeof(data),&n,0)==CB_OK);say("sample resource ");CB_REQUIRE(cb_write_all(&console,output,data,n)==CB_OK);CB_REQUIRE(cb_system_release(file)==CB_OK);cb_exit(0);
}
