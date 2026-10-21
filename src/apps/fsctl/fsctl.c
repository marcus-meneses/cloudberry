#include "../../sdk/application.h"
#include "../../sdk/stream.h"
static struct cb_client client;
static cb_handle_t output;
static void say(const char *s){CB_REQUIRE(cb_write_all(&client,output,s,cb_length(s))==CB_OK);}
static void number(uint64_t n,unsigned base){char a[64],b[64];size_t at=0;do{a[at++]='0'+n%base;n/=base;}while(n);for(size_t i=0;i<at;++i)b[i]=a[at-1-i];CB_REQUIRE(cb_write_all(&client,output,b,at)==CB_OK);}
static int equal(const char *a,const char *b){while(*a && *a==*b){++a;++b;}return *a==*b;}
static int parse(const char *s,unsigned base,uint64_t *value){if(!s || !*s)return 0;uint64_t n=0;while(*s){unsigned d=*s++-'0';if(d>=base || n>(UINT64_MAX-d)/base)return 0;n=n*base+d;}*value=n;return 1;}
__attribute__((noreturn)) void _start(const struct cb_startup *packet){struct cb_application_start start;CB_REQUIRE(cb_application_start(packet,&start));output=cb_standard(1);const char *op=cb_application_argument(&start,1),*arg=cb_application_argument(&start,2),*name=NULL;uint64_t mode=0,uid=0,gid=0;cb_status_t status=CB_BAD_REQUEST;
    if(op && equal(op,"stat") && start.header.argc==3)name=arg;
    else if(op && equal(op,"chmod") && start.header.argc==4 && parse(arg,8,&mode) && mode<=0777)name=cb_application_argument(&start,3);
    else if(op && equal(op,"chown") && start.header.argc==5 && parse(arg,10,&uid) && parse(cb_application_argument(&start,3),10,&gid))name=cb_application_argument(&start,4);
    if(!name){say("fsctl stat PATH | chmod OCTAL PATH | chown UID GID PATH\n");cb_exit(CB_BAD_REQUEST);}char path[4096],cwd[4096];if(start.header.cwd_bytes>=sizeof(cwd))cb_exit(CB_BAD_REQUEST);cb_copy(cwd,start.cwd,start.header.cwd_bytes);cwd[start.header.cwd_bytes]=0;if(!cb_path(path,sizeof(path),cwd,name))cb_exit(CB_BAD_REQUEST);cb_handle_t fs=cb_acquire(CB_FILESYSTEM_NAME,(struct cb_interface_id)CB_FILESYSTEM_INTERFACE_ID);
    if(equal(op,"stat")){status=cb_fs_call(&client,fs,CB_FILESYSTEM_METADATA,path,0,0);if(!status){struct cb_filesystem_metadata m;cb_copy(&m,cb_result(&client),sizeof(m));say("uid=");number(m.uid,10);say(" gid=");number(m.gid,10);say(" mode=");number(m.mode,8);say(" bytes=");number(m.bytes,10);say(m.directory?" directory\n":" file\n");}}
    else if(equal(op,"chmod"))status=cb_fs_call(&client,fs,CB_FILESYSTEM_CHMOD,path,mode,0);else status=cb_fs_chown(&client,fs,path,uid,gid);
    if(status){say("fsctl: status ");number(status,10);say("\n");}cb_exit(status);
}
