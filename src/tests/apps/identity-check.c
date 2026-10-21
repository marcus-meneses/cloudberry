#include "../../sdk/application.h"
#include "../../sdk/stream.h"
#include "../../interfaces/identity.h"
#include "../../interfaces/identity-format.h"
static struct cb_client client;
static cb_handle_t identity,memory;
static __attribute__((noreturn)) void fail(unsigned line){char message[48]="IDENTITY FAIL ";size_t n=14;char digits[10];unsigned count=0;do{digits[count++]='0'+line%10;line/=10;}while(line);while(count)message[n++]=digits[--count];message[n++]='\n';cb_write_all(&client,cb_standard(1),message,n);cb_exit(1);}
#undef CB_REQUIRE
#define CB_REQUIRE(x) do{if(!(x))fail(__LINE__);}while(0)
static void *allocate(size_t bytes){struct cb_memory_request request={.bytes=bytes};
    CB_REQUIRE(cb_invoke(&client,memory,(struct cb_interface_id)CB_MEMORY_INTERFACE_ID,CB_MEMORY_ALLOCATE,&request,sizeof(request),0)==CB_OK);struct cb_memory_result result;cb_copy(&result,cb_result(&client),sizeof(result));return (void *)(uintptr_t)result.address;}
static cb_status_t call(unsigned op,const void *p,size_t n){return cb_invoke(&client,identity,(struct cb_interface_id)CB_IDENTITY_INTERFACE_ID,op,p,n,0);}
static struct cb_identity_info info(void){CB_REQUIRE(call(CB_IDENTITY_INFO,NULL,0)==CB_OK);struct cb_identity_info result;cb_copy(&result,cb_result(&client),sizeof(result));return result;}
static void seal(struct id_blob *b,uint64_t generation,uint64_t next){id_put64(b->data+16,b->bytes);id_put64(b->data+24,generation);id_put64(b->data+40,next);id_put32(b->data+48,id_crc(b->data,b->bytes,48));}
static struct id_blob users(void){uint64_t cursor=0;
    CB_REQUIRE(call(CB_IDENTITY_USERS,&cursor,8)==CB_OK);const unsigned char *p=cb_result(&client);uint64_t bytes=id_u64(p+8+16);struct id_blob result={allocate(bytes+64),bytes};
    CB_REQUIRE(call(CB_IDENTITY_USERS,&cursor,8)==CB_OK);for(;;){struct cb_reply_header h;cb_copy(&h,client.result,sizeof(h));
    CB_REQUIRE(h.result_bytes>=8);uint64_t next;cb_copy(&next,cb_result(&client),8);
    CB_REQUIRE(next>cursor && next<=bytes && next-cursor==h.result_bytes-8);cb_copy(result.data+cursor,(unsigned char *)cb_result(&client)+8,next-cursor);cursor=next;if(cursor==bytes)break;
    CB_REQUIRE(call(CB_IDENTITY_USERS,&cursor,8)==CB_OK);}return result;}
static uint64_t begin(uint64_t generation,size_t bytes){struct cb_identity_begin request={.generation=generation,.bytes={bytes,0,0}};
    CB_REQUIRE(call(CB_IDENTITY_BEGIN,&request,sizeof(request))==CB_OK);uint64_t id;cb_copy(&id,cb_result(&client),8);return id;}
static void stage(uint64_t tx,const struct id_blob *b){unsigned char frame[sizeof(struct cb_identity_stage)+4096];for(size_t at=0;at<b->bytes;){size_t n=b->bytes-at;if(n>4096)n=4096;struct cb_identity_stage request={.transaction=tx,.offset=at};cb_copy(frame,&request,sizeof(request));cb_copy(frame+sizeof(request),b->data+at,n);
    CB_REQUIRE(call(CB_IDENTITY_STAGE,frame,sizeof(request)+n)==CB_OK);at+=n;}}
static void child(const struct cb_application_start *start){
    CB_REQUIRE(call(CB_IDENTITY_SELF,NULL,0)==CB_OK);struct cb_credentials who;cb_copy(&who,cb_result(&client),sizeof(who));
    const char *mode=cb_application_argument(start,1);
    if(mode && mode[0]=='g'){CB_REQUIRE(who.uid==1 && who.gid==2);cb_exit(0);}
    CB_REQUIRE(!who.uid);cb_handle_t fs=cb_acquire(CB_FILESYSTEM_NAME,(struct cb_interface_id)CB_FILESYSTEM_INTERFACE_ID);
    CB_REQUIRE(cb_fs_call(&client,fs,CB_FILESYSTEM_OPEN,"/tmp/root-private.bin",CB_FILE_RECREATE,0)==CB_OK);cb_handle_t opened;cb_copy(&opened,cb_result(&client),8);
    CB_REQUIRE(cb_write_all(&client,opened,"secret",6)==CB_OK);cb_system_release(opened);
    CB_REQUIRE(cb_fs_call(&client,fs,CB_FILESYSTEM_OPEN,"/tmp/root-private.bin",CB_FILE_READ,0)==CB_OK);cb_copy(&opened,cb_result(&client),8);
    struct {struct cb_identity_password request;char password;} auth={.request={.uid=1,.password_bytes=1},.password='x'};
    CB_REQUIRE(call(CB_IDENTITY_AUTHENTICATE,&auth,sizeof(auth.request)+1)==CB_OK);
    CB_REQUIRE(call(CB_IDENTITY_SELF,NULL,0)==CB_OK);cb_copy(&who,cb_result(&client),sizeof(who));
    CB_REQUIRE(who.uid==1 && who.gid==2);
    struct {struct cb_identity_password request;char password[2];} wrong={.request={.uid=1,.old_bytes=1,.password_bytes=1},.password={'z','y'}};
    CB_REQUIRE(call(CB_IDENTITY_PASSWORD,&wrong,sizeof(wrong.request)+2)==CB_DENIED);
    CB_REQUIRE(call(CB_IDENTITY_SELF,NULL,0)==CB_OK);cb_copy(&who,cb_result(&client),sizeof(who));CB_REQUIRE(who.uid==1);
    struct cb_identity_begin begin={0};
    CB_REQUIRE(call(CB_IDENTITY_BEGIN,&begin,sizeof(begin))==CB_DENIED);struct cb_identity_credential forbidden={.uid=0};
    CB_REQUIRE(call(CB_IDENTITY_CREDENTIAL,&forbidden,sizeof(forbidden))==CB_DENIED);
    unsigned char buffer[8];size_t n=0;
    CB_REQUIRE(cb_stream_read(&client,opened,buffer,sizeof(buffer),&n,0)==CB_DENIED);cb_system_release(opened);
    CB_REQUIRE(cb_fs_call(&client,fs,CB_FILESYSTEM_STAT,"/system/root/_apps",0,0)==CB_DENIED);
    CB_REQUIRE(cb_fs_call(&client,fs,CB_FILESYSTEM_OPEN,"/tmp/fixture-owned.bin",CB_FILE_RECREATE,0)==CB_OK);cb_copy(&opened,cb_result(&client),8);
    CB_REQUIRE(cb_write_all(&client,opened,"owned",5)==CB_OK);cb_system_release(opened);
    CB_REQUIRE(cb_fs_call(&client,fs,CB_FILESYSTEM_REMOVE,"/tmp/root-private.bin",0,0)==CB_DENIED);
    cb_handle_t apps=cb_acquire(CB_APPLICATIONS_NAME,(struct cb_interface_id)CB_APPLICATIONS_INTERFACE_ID);unsigned char frame[256];const char name[]="identity-check",args[]="identity-check\0grandchild\0";struct cb_application_launch_streams request={.base={.version={sizeof(request),1,1},.name_bytes=sizeof(name)-1,.argc=2,.arguments_bytes=sizeof(args)-1,.cwd_bytes=1},.streams={0,cb_standard(1),cb_standard(2)}};size_t at=sizeof(request);cb_copy(frame,&request,at);cb_copy(frame+at,name,sizeof(name)-1);at+=sizeof(name)-1;cb_copy(frame+at,args,sizeof(args)-1);at+=sizeof(args)-1;frame[at++]='/';
    CB_REQUIRE(cb_invoke(&client,apps,(struct cb_interface_id)CB_APPLICATIONS_INTERFACE_ID,CB_APPLICATION_LAUNCH,frame,at,0)==CB_OK);struct cb_application_ref ref;cb_copy(&ref,cb_result(&client),8);
    CB_REQUIRE(cb_invoke(&client,apps,(struct cb_interface_id)CB_APPLICATIONS_INTERFACE_ID,CB_APPLICATION_WAIT,&ref,sizeof(ref),0)==CB_OK);struct cb_application_status status;cb_copy(&status,cb_result(&client),sizeof(status));
    CB_REQUIRE(status.state==CB_APPLICATION_EXITED && !status.exit_code);
    CB_REQUIRE(cb_invoke(&client,apps,(struct cb_interface_id)CB_APPLICATIONS_INTERFACE_ID,CB_APPLICATION_RELEASE,&ref,sizeof(ref),0)==CB_OK);cb_exit(0);
}
__attribute__((noreturn)) void _start(const struct cb_startup *packet){
    struct cb_application_start startup;
    CB_REQUIRE(cb_application_start(packet,&startup));identity=cb_acquire(CB_IDENTITY_NAME,(struct cb_interface_id)CB_IDENTITY_INTERFACE_ID);memory=cb_acquire(CB_MEMORY_NAME,(struct cb_interface_id)CB_MEMORY_INTERFACE_ID);if(startup.header.argc>1)child(&startup);struct cb_identity_info initial=info();
    CB_REQUIRE(initial.users==1 && initial.groups==3 && initial.next_uid && initial.next_gid>=3);
    cb_handle_t fs=cb_acquire(CB_FILESYSTEM_NAME,(struct cb_interface_id)CB_FILESYSTEM_INTERFACE_ID);
    CB_REQUIRE(cb_fs_call(&client,fs,CB_FILESYSTEM_STAT,"/system/config/identity/credentials.bin",0,0)==CB_DENIED && cb_fs_call(&client,fs,CB_FILESYSTEM_OPEN,"/system/config/identity/users.bin",CB_FILE_READ,0)==CB_DENIED);
    struct id_blob base=users(),added={allocate(base.bytes+39),base.bytes+39};cb_copy(added.data,base.data,base.bytes);unsigned char *entry=added.data+base.bytes;for(unsigned i=0;i<39;++i)entry[i]=0;id_put32(entry,39);id_put32(entry+4,1);id_put64(entry+8,initial.next_uid);id_put64(entry+16,2);id_put32(entry+24,7);cb_copy(entry+32,"fixture",7);id_put64(added.data+32,2);seal(&added,initial.generation+1,initial.next_uid+1);
    uint64_t tx=begin(initial.generation,added.bytes);
    CB_REQUIRE(call(CB_IDENTITY_COMMIT,&tx,8)==CB_BAD_REQUEST);stage(tx,&added);struct cb_identity_credential credential={.transaction=tx,.uid=initial.next_uid,.algorithm=1,.rounds=600000};for(unsigned i=0;i<32;++i){credential.salt[i]=(unsigned char)(i+1);credential.hash[i]=(unsigned char)(i+33);}CB_REQUIRE(call(CB_IDENTITY_CREDENTIAL,&credential,sizeof(credential))==CB_OK);
    CB_REQUIRE(call(CB_IDENTITY_COMMIT,&tx,8)==CB_OK);struct cb_identity_info current=info();
    CB_REQUIRE(current.users==2 && current.next_uid==initial.next_uid+1 && current.generation==initial.generation+1);
    struct {struct cb_identity_password request;char password;} password={.request={.uid=initial.next_uid,.password_bytes=1},.password='x'};
    CB_REQUIRE(call(CB_IDENTITY_PASSWORD,&password,sizeof(password.request)+1)==CB_OK);current=info();
    cb_handle_t apps=cb_acquire(CB_APPLICATIONS_NAME,(struct cb_interface_id)CB_APPLICATIONS_INTERFACE_ID);unsigned char frame[256];const char name[]="identity-check",args[]="identity-check\0child\0";struct cb_application_launch_streams request={.base={.version={sizeof(request),1,1},.name_bytes=sizeof(name)-1,.argc=2,.arguments_bytes=sizeof(args)-1,.cwd_bytes=1},.streams={0,cb_standard(1),cb_standard(2)}};size_t at=sizeof(request);cb_copy(frame,&request,at);cb_copy(frame+at,name,sizeof(name)-1);at+=sizeof(name)-1;cb_copy(frame+at,args,sizeof(args)-1);at+=sizeof(args)-1;frame[at++]='/';
    CB_REQUIRE(cb_invoke(&client,apps,(struct cb_interface_id)CB_APPLICATIONS_INTERFACE_ID,CB_APPLICATION_LAUNCH,frame,at,0)==CB_OK);struct cb_application_ref ref;cb_copy(&ref,cb_result(&client),8);
    CB_REQUIRE(cb_invoke(&client,apps,(struct cb_interface_id)CB_APPLICATIONS_INTERFACE_ID,CB_APPLICATION_WAIT,&ref,sizeof(ref),0)==CB_OK);struct cb_application_status child_status;cb_copy(&child_status,cb_result(&client),sizeof(child_status));
    CB_REQUIRE(child_status.state==CB_APPLICATION_EXITED && !child_status.exit_code);
    CB_REQUIRE(cb_invoke(&client,apps,(struct cb_interface_id)CB_APPLICATIONS_INTERFACE_ID,CB_APPLICATION_RELEASE,&ref,sizeof(ref),0)==CB_OK);
    seal(&base,current.generation+1,current.next_uid);tx=begin(current.generation,base.bytes);stage(tx,&base);credential=(struct cb_identity_credential){.transaction=tx,.uid=initial.next_uid};
    CB_REQUIRE(call(CB_IDENTITY_CREDENTIAL,&credential,sizeof(credential))==CB_OK && call(CB_IDENTITY_COMMIT,&tx,8)==CB_OK);current=info();
    CB_REQUIRE(current.users==1 && current.generation==initial.generation+3 && current.next_uid==initial.next_uid+1);
    seal(&added,current.generation+1,current.next_uid);tx=begin(current.generation,added.bytes);stage(tx,&added);
    CB_REQUIRE(call(CB_IDENTITY_COMMIT,&tx,8)==CB_BAD_REQUEST && call(CB_IDENTITY_ABORT,&tx,8)==CB_OK);
    CB_REQUIRE(info().generation==current.generation);
    CB_REQUIRE(cb_write_all(&client,cb_standard(1),"IDENTITY PASS\n",14)==CB_OK);cb_exit(0);
}
