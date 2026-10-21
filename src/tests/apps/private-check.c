#include "../../sdk/application.h"
#include "../../sdk/stream.h"
#include "../../interfaces/sessions.h"
static struct cb_client client;
static cb_handle_t out;
static __attribute__((noreturn)) void fail(unsigned line){char b[64]="PRIVATE FAIL ";size_t n=13;char digits[20];unsigned at=0;do{digits[at++]='0'+line%10;line/=10;}while(line);while(at)b[n++]=digits[--at];b[n++]='\n';cb_write_all(&client,out,b,n);cb_exit(1);}
#undef CB_REQUIRE
#define CB_REQUIRE(x) do{if(!(x))fail(__LINE__);}while(0)
__attribute__((noreturn)) void _start(const struct cb_startup *packet){struct cb_application_start start;CB_REQUIRE(cb_application_start(packet,&start));out=cb_standard(1);cb_handle_t apps=cb_acquire(CB_APPLICATIONS_NAME,(struct cb_interface_id)CB_APPLICATIONS_INTERFACE_ID),fs=cb_acquire(CB_FILESYSTEM_NAME,(struct cb_interface_id)CB_FILESYSTEM_INTERFACE_ID);
    CB_REQUIRE(cb_invoke(&client,apps,(struct cb_interface_id)CB_APPLICATIONS_INTERFACE_ID,CB_APPLICATION_DATA,NULL,0,0)==CB_OK);struct cb_reply_header h;cb_copy(&h,client.result,sizeof(h));char path[4096];CB_REQUIRE(h.result_bytes && h.result_bytes<sizeof(path)-16);cb_copy(path,cb_result(&client),h.result_bytes);cb_copy(path+h.result_bytes,"/secret",8);
    if(start.header.argc==2){
        CB_REQUIRE(cb_fs_call(&client,fs,CB_FILESYSTEM_OPEN,path,CB_FILE_READ,0)==CB_OK);cb_handle_t old_file;cb_copy(&old_file,cb_result(&client),8);
        cb_handle_t session=cb_acquire(CB_SESSIONS_NAME,(struct cb_interface_id)CB_SESSIONS_INTERFACE_ID);CB_REQUIRE(cb_invoke(&client,session,(struct cb_interface_id)CB_SESSIONS_INTERFACE_ID,CB_SESSIONS_BEGIN,NULL,0,0)==CB_OK);
        CB_REQUIRE(cb_invoke(&client,session,(struct cb_interface_id)CB_SESSIONS_INTERFACE_ID,CB_SESSIONS_END,NULL,0,0)==CB_OK);struct cb_session_status drain;cb_copy(&drain,cb_result(&client),sizeof(drain));CB_REQUIRE(drain.closing && !drain.running && !drain.stop_failures);
        unsigned char byte;size_t bytes=0;CB_REQUIRE(cb_stream_read(&client,old_file,&byte,1,&bytes,0)==CB_REVOKED);cb_handle_t denied=0;CB_REQUIRE(cb_system_get_handle(CB_FILESYSTEM_NAME,sizeof(CB_FILESYSTEM_NAME)-1,&(struct cb_interface_id)CB_FILESYSTEM_INTERFACE_ID,1,0,&denied)==CB_REVOKED);
        CB_REQUIRE(cb_invoke(&client,apps,(struct cb_interface_id)CB_APPLICATIONS_INTERFACE_ID,CB_APPLICATION_DATA,NULL,0,0)==CB_REVOKED);CB_REQUIRE(cb_system_release(old_file)==CB_OK);cb_exit(0);
    }
    cb_status_t opened=cb_fs_call(&client,fs,CB_FILESYSTEM_OPEN,path,CB_FILE_RECREATE,0);if(opened){char message[32]="OPEN STATUS ";message[12]='0'+opened/10;message[13]='0'+opened%10;message[14]='\n';cb_write_all(&client,out,message,15);cb_write_all(&client,out,path,cb_length(path));cb_write_all(&client,out,"\n",1);fail(__LINE__);}
    cb_handle_t file;cb_copy(&file,cb_result(&client),8);
    CB_REQUIRE(cb_write_all(&client,file,"private-data\n",13)==CB_OK);
    CB_REQUIRE(cb_system_release(file)==CB_OK);
    CB_REQUIRE(cb_fs_call(&client,fs,CB_FILESYSTEM_OPEN,"/home/alice/_apps/terminal.data/secret",CB_FILE_READ,0)==CB_DENIED);
    CB_REQUIRE(cb_fs_call(&client,fs,CB_FILESYSTEM_METADATA,"/home/alice/_apps/terminal.data",0,0)==CB_DENIED);
    CB_REQUIRE(cb_fs_call(&client,fs,CB_FILESYSTEM_CHMOD,"/home/alice/_apps/terminal.data",0777,0)==CB_DENIED);
    CB_REQUIRE(cb_fs_call(&client,fs,CB_FILESYSTEM_OPEN,path,CB_FILE_READ,0)==CB_OK);cb_copy(&file,cb_result(&client),8);
    struct cb_application_launch_streams launch={.base={.version={sizeof(launch),1,1},.name_bytes=3,.argc=1,.arguments_bytes=4,.cwd_bytes=1},.streams={file,out,cb_standard(2)}};unsigned char frame[sizeof(launch)+8];cb_copy(frame,&launch,sizeof(launch));cb_copy(frame+sizeof(launch),"catcat\0/",8);
    CB_REQUIRE(cb_invoke(&client,apps,(struct cb_interface_id)CB_APPLICATIONS_INTERFACE_ID,CB_APPLICATION_LAUNCH,frame,sizeof(frame),0)==CB_OK);struct cb_application_ref ref;cb_copy(&ref,cb_result(&client),sizeof(ref));CB_REQUIRE(cb_invoke(&client,apps,(struct cb_interface_id)CB_APPLICATIONS_INTERFACE_ID,CB_APPLICATION_WAIT,&ref,sizeof(ref),0)==CB_OK);struct cb_application_status status;cb_copy(&status,cb_result(&client),sizeof(status));CB_REQUIRE(status.exit_code==CB_DENIED);CB_REQUIRE(cb_invoke(&client,apps,(struct cb_interface_id)CB_APPLICATIONS_INTERFACE_ID,CB_APPLICATION_RELEASE,&ref,sizeof(ref),0)==CB_OK);CB_REQUIRE(cb_system_release(file)==CB_OK);
    launch=(struct cb_application_launch_streams){.base={.version={sizeof(launch),1,1},.name_bytes=13,.argc=2,.arguments_bytes=23,.cwd_bytes=1},.streams={0,out,cb_standard(2)}};unsigned char revoke_frame[sizeof(launch)+37];cb_copy(revoke_frame,&launch,sizeof(launch));cb_copy(revoke_frame+sizeof(launch),"private-checkprivate-check\0--revoke\0/",37);
    CB_REQUIRE(cb_invoke(&client,apps,(struct cb_interface_id)CB_APPLICATIONS_INTERFACE_ID,CB_APPLICATION_LAUNCH,revoke_frame,sizeof(revoke_frame),0)==CB_OK);cb_copy(&ref,cb_result(&client),sizeof(ref));CB_REQUIRE(cb_invoke(&client,apps,(struct cb_interface_id)CB_APPLICATIONS_INTERFACE_ID,CB_APPLICATION_WAIT,&ref,sizeof(ref),0)==CB_OK);cb_copy(&status,cb_result(&client),sizeof(status));CB_REQUIRE(status.exit_code==0);CB_REQUIRE(cb_invoke(&client,apps,(struct cb_interface_id)CB_APPLICATIONS_INTERFACE_ID,CB_APPLICATION_RELEASE,&ref,sizeof(ref),0)==CB_OK);
    CB_REQUIRE(cb_write_all(&client,out,"PRIVATE PASS\n",13)==CB_OK);cb_exit(0);
}
