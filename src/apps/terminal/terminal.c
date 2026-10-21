#include "../../sdk/application.h"
#include "../../sdk/stream.h"
#include "../../interfaces/keyboard.h"
#include "../../interfaces/text.h"
#include "../../interfaces/io.h"
#include "parser.h"
#include "editor.h"
struct command {struct command *next;char **argv;size_t argc;char *file;unsigned mode;cb_handle_t input,output;uint64_t process;unsigned done;struct cb_client *completion;};
static struct cb_client client,keyboard_client;
static cb_handle_t memory,keyboard,text,applications,streams,filesystem;
static char cwd[4096]="/",line[CB_FRAME_LIMIT-1024];
static struct term_editor editor={.data=line,.capacity=sizeof(line)};
static uint32_t columns;
static struct command *orphans;
static struct cb_keyboard_read keyread={.version={sizeof(struct cb_keyboard_read_focus),1,1},.max_events=32};
static void *allocate(size_t bytes){struct cb_memory_request r={.bytes=bytes};CB_REQUIRE(cb_invoke(&client,memory,(struct cb_interface_id)CB_MEMORY_INTERFACE_ID,CB_MEMORY_ALLOCATE,&r,sizeof(r),0)==CB_OK);struct cb_memory_result result;cb_copy(&result,cb_result(&client),sizeof(result));return (void *)(uintptr_t)result.address;}
static void release(void *p){if(p){struct cb_memory_request r={.address=(uintptr_t)p};CB_REQUIRE(cb_invoke(&client,memory,(struct cb_interface_id)CB_MEMORY_INTERFACE_ID,CB_MEMORY_FREE,&r,sizeof(r),0)==CB_OK);}}
static void raw_output(const char *data,size_t bytes){while(bytes){size_t n=bytes>4096?4096:bytes;CB_REQUIRE(cb_invoke(&client,text,(struct cb_interface_id)CB_TEXT_INTERFACE_ID,CB_TEXT_WRITE,data,n,0)==CB_OK);data+=n;bytes-=n;}}
static unsigned input_active,input_suspended;static uint64_t focus_generation;
static void input_pause(void),input_begin(void);
static void output(const char *data,size_t bytes){unsigned restore=input_active;if(restore)input_pause();raw_output(data,bytes);if(restore)input_begin();}
static void print(const char *s){output(s,cb_length(s));}
static void error(cb_status_t status){static const char *names[]={"ok","permission denied","unavailable","timeout","revoked","bad handle","invalid request","version mismatch","buffer too small","resource failure","provider failed","busy","input reset","name in use","would block","broken pipe","read-only filesystem","not found"};print(status<sizeof(names)/sizeof(names[0])?names[status]:"operation failed");print("\n");}
static struct cb_stream_pair pipe_create(void){CB_REQUIRE(cb_invoke(&client,streams,(struct cb_interface_id)CB_STREAM_INTERFACE_ID,CB_STREAM_PIPE,NULL,0,0)==CB_OK);struct cb_stream_pair pair;cb_copy(&pair,cb_result(&client),sizeof(pair));return pair;}
static void close_handle(cb_handle_t *h){if(*h){CB_REQUIRE(cb_system_release(*h)==CB_OK);*h=0;}}
static void prompt(void);
static void help(void){print("cd  clear  help  exit\n");uint64_t cursor=0;unsigned col=0;for(;;){CB_REQUIRE(cb_invoke(&client,applications,(struct cb_interface_id)CB_APPLICATIONS_INTERFACE_ID,CB_APPLICATION_LIST,&cursor,8,0)==CB_OK);struct cb_reply_header r;cb_copy(&r,client.result,sizeof(r));if(!r.result_bytes)break;struct cb_application_entry e;cb_copy(&e,cb_result(&client),sizeof(e));cursor=e.cursor;output((const char *)cb_result(&client)+sizeof(e),e.name_bytes);uint32_t width=columns/4;if(!width)width=1;static const char spaces[]="                                                                ";size_t padding=e.name_bytes<width?width-e.name_bytes:1;while(padding){size_t n=padding>sizeof(spaces)-1?sizeof(spaces)-1:padding;output(spaces,n);padding-=n;}if(++col==4){print("\n");col=0;}}if(col)print("\n");}
static cb_status_t launch(struct command *c){unsigned char buffer[CB_FRAME_LIMIT];size_t name=cb_length(c->argv[0]),args=0;for(size_t i=0;i<c->argc;++i)args+=cb_length(c->argv[i])+1;size_t dir=cb_length(cwd),total=sizeof(struct cb_application_launch_streams)+name+args+dir;if(total>sizeof(buffer)-1024)return CB_BAD_REQUEST;struct cb_application_launch_streams request={.base={.version={sizeof(request),1,1},.name_bytes=name,.argc=c->argc,.arguments_bytes=args,.cwd_bytes=dir},.streams={c->input,c->output,0}};
    /* Every command receives a separate stderr pipe shared by the chain. */
    extern cb_handle_t terminal_error_writer;request.streams[2]=terminal_error_writer;cb_copy(buffer,&request,sizeof(request));size_t at=sizeof(request);cb_copy(buffer+at,c->argv[0],name);at+=name;for(size_t i=0;i<c->argc;++i){size_t n=cb_length(c->argv[i])+1;cb_copy(buffer+at,c->argv[i],n);at+=n;}cb_copy(buffer+at,cwd,dir);c->completion=allocate(sizeof(*c->completion));cb_status_t status=cb_invoke(&client,applications,(struct cb_interface_id)CB_APPLICATIONS_INTERFACE_ID,CB_APPLICATION_LAUNCH,buffer,total,0);if(!status){cb_copy(&c->process,cb_result(&client),8);struct cb_application_ref ref={c->process};status=cb_submit(c->completion,applications,(struct cb_interface_id)CB_APPLICATIONS_INTERFACE_ID,CB_APPLICATION_WAIT,&ref,sizeof(ref));}return status;
}
cb_handle_t terminal_error_writer;
static cb_status_t pump(cb_handle_t h,int *eof){unsigned char buffer[4096];size_t n=0;cb_status_t status=cb_stream_read(&client,h,buffer,sizeof(buffer),&n,CB_STREAM_NONBLOCK);if(status==CB_WOULD_BLOCK)return CB_OK;if(status)return status;if(!n)*eof=1;else output((const char *)buffer,n);return CB_OK;}
static void request_keyboard(void){if(!keyboard_client.pending.serial){struct cb_keyboard_read_focus read={keyread,focus_generation};CB_REQUIRE(cb_submit(&keyboard_client,keyboard,(struct cb_interface_id)CB_KEYBOARD_INTERFACE_ID,CB_KEYBOARD_READ,&read,sizeof(read))==CB_OK);}}
static unsigned events(struct cb_keyboard_event *out,size_t max){
    request_keyboard();cb_status_t status=cb_collect(&keyboard_client);if(status==CB_WOULD_BLOCK && !keyboard_client.collected)return 0;
    if(status==CB_INPUT_RESYNC){struct cb_keyboard_batch b;cb_copy(&b,cb_result(&keyboard_client),sizeof(b));keyread.provider_epoch=b.provider_epoch;keyread.after_sequence=b.next_sequence;CB_REQUIRE(b.version.bytes==sizeof(struct cb_keyboard_batch_focus));cb_copy(&focus_generation,(unsigned char *)cb_result(&keyboard_client)+sizeof(b),sizeof(focus_generation));input_suspended=(b.flags&CB_KEYBOARD_FOCUS_SUSPENDED)!=0;if(input_suspended)input_active=0;request_keyboard();return 0;}
    if(status!=CB_OK){print("keyboard input failed\n");cb_exit(status);}struct cb_keyboard_batch batch;cb_copy(&batch,cb_result(&keyboard_client),sizeof(batch));CB_REQUIRE(batch.count<=max && batch.version.bytes+(uint64_t)batch.count*sizeof(*out)<=keyboard_client.bytes-sizeof(struct cb_reply_header));cb_copy(out,(unsigned char *)cb_result(&keyboard_client)+batch.version.bytes,batch.count*sizeof(*out));keyread.provider_epoch=batch.provider_epoch;keyread.after_sequence=batch.next_sequence;
    /* Keep the next provider read outstanding while rendering this batch. */
    request_keyboard();return batch.count;
}
/* One queue spans idle/foreground transitions. Reading a batch advances the
 * provider cursor, so nested execution must retain every unconsumed event. */
static struct cb_keyboard_event queued_keys[32];
static unsigned queued_count,queued_at;
static int next_key(struct cb_keyboard_event *out){if(queued_at==queued_count){queued_count=events(queued_keys,32);queued_at=0;if(!queued_count)return 0;}*out=queued_keys[queued_at++];return 1;}
#include "editing.inc"
static int complete(struct command *c){
    if(c->done || !c->process)return 1;
    if(!c->completion->pending.serial){struct cb_application_ref ref={c->process};CB_REQUIRE(cb_submit(c->completion,applications,(struct cb_interface_id)CB_APPLICATIONS_INTERFACE_ID,CB_APPLICATION_WAIT,&ref,sizeof(ref))==CB_OK);}
    cb_status_t status=cb_collect(c->completion);if(status==CB_WOULD_BLOCK && !c->completion->collected)return 0;CB_REQUIRE(status==CB_OK);
    struct cb_application_status result;cb_copy(&result,cb_result(c->completion),sizeof(result));struct cb_application_ref ref={c->process};CB_REQUIRE(cb_invoke(&client,applications,(struct cb_interface_id)CB_APPLICATIONS_INTERFACE_ID,CB_APPLICATION_RELEASE,&ref,sizeof(ref),0)==CB_OK);c->process=0;c->done=1;if(result.state==CB_APPLICATION_FAULTED)print("application fault\n");return 1;
}
static void reap_orphans(void){struct command **at=&orphans;while(*at){struct command *c=*at;if(complete(c)){*at=c->next;release(c->completion);release(c);continue;}at=&c->next;}}
enum {WAIT_LIMIT=(CB_FRAME_LIMIT-512)/sizeof(struct cb_wait_item)};
static void wait_activity(struct command *commands,cb_handle_t out,cb_handle_t err,cb_handle_t input){
    struct cb_wait_item items[WAIT_LIMIT];size_t count=0;
    /* Already-buffered key events must be handled without sleeping. */
    if(!input){request_keyboard();if(queued_at<queued_count)return;items[count++]=(struct cb_wait_item){.kind=CB_WAIT_REQUEST,.request=keyboard_client.pending};}
    if(out)items[count++]=(struct cb_wait_item){.kind=CB_WAIT_READ,.handle=out};
    if(err)items[count++]=(struct cb_wait_item){.kind=CB_WAIT_READ,.handle=err};
    if(input)items[count++]=(struct cb_wait_item){.kind=CB_WAIT_WRITE,.handle=input};
    for(unsigned group=0;group<2;++group)for(struct command *c=group?orphans:commands;c;c=c->next)if(c->process && c->completion->pending.serial){CB_REQUIRE(count<WAIT_LIMIT);items[count++]=(struct cb_wait_item){.kind=CB_WAIT_REQUEST,.request=c->completion->pending};}
    uint64_t ready;cb_status_t status=cb_system_wait(items,count,input_blink_deadline(),&ready);CB_REQUIRE(status==CB_OK || status==CB_TIMEOUT);if(status==CB_TIMEOUT)input_blink_tick();
}
static void execute(void){size_t size=editor.length+1;char *storage=allocate(size);struct term_token *tokens=allocate(size*sizeof(*tokens));size_t count=0;struct command *first=NULL,**tail=&first;cb_status_t status=CB_OK;
    if(!term_tokens(line,storage,tokens,size,&count)){error(CB_BAD_REQUEST);goto cleanup;}if(!count)goto cleanup;
    for(size_t i=0;i<count;){struct command *c=allocate(sizeof(*c));c->argv=allocate((count+1)*sizeof(*c->argv));*tail=c;tail=&c->next;while(i<count && tokens[i].kind!=TERM_PIPE){if(tokens[i].kind==TERM_WORD)c->argv[c->argc++]=tokens[i++].text;else {unsigned mode=tokens[i++].kind;if(c->file || i==count || tokens[i].kind!=TERM_WORD){status=CB_BAD_REQUEST;break;}c->file=tokens[i++].text;c->mode=mode==TERM_RECREATE?CB_FILE_RECREATE:CB_FILE_APPEND;}}if(status || !c->argc || (c->file && i<count)){status=CB_BAD_REQUEST;break;}if(i<count && ++i==count){status=CB_BAD_REQUEST;break;}}
    if(status){error(status);goto cleanup;}
    size_t waiting=4;for(struct command *c=first;c;c=c->next)++waiting;for(struct command *c=orphans;c;c=c->next)++waiting;if(waiting>WAIT_LIMIT){error(CB_BAD_REQUEST);goto cleanup;}
    if(!first->next && !first->file){const char *cmd=first->argv[0];if(!__builtin_strcmp(cmd,"exit")){struct cb_abandon_result result;if(keyboard_client.pending.serial)CB_REQUIRE(cb_abandon(&keyboard_client,&result)==CB_OK);cb_exit(0);}if(!__builtin_strcmp(cmd,"help")){help();goto cleanup;}if(!__builtin_strcmp(cmd,"clear")){CB_REQUIRE(cb_invoke(&client,text,(struct cb_interface_id)CB_TEXT_INTERFACE_ID,CB_TEXT_CLEAR,NULL,0,0)==CB_OK);goto cleanup;}if(!__builtin_strcmp(cmd,"cd")){char path[4096];if(first->argc>2 || !cb_path(path,sizeof(path),cwd,first->argc==2?first->argv[1]:"/")){error(CB_BAD_REQUEST);goto cleanup;}status=cb_fs_call(&client,filesystem,CB_FILESYSTEM_STAT,path,0,0);if(!status){struct cb_filesystem_stat st;cb_copy(&st,cb_result(&client),sizeof(st));if(!st.directory)status=CB_BAD_REQUEST;else cb_copy(cwd,path,cb_length(path)+1);}if(status)error(status);goto cleanup;}}
    /* Resolve every output destination before starting any member of the chain. */
    struct cb_stream_pair input=pipe_create(),out=pipe_create(),err=pipe_create();terminal_error_writer=err.writer;first->input=input.reader;for(struct command *c=first;c;c=c->next){if(c->next){struct cb_stream_pair p=pipe_create();c->output=p.writer;c->next->input=p.reader;}else c->output=out.writer;if(c->file){char path[4096];if(!cb_path(path,sizeof(path),cwd,c->file))status=CB_BAD_REQUEST;else status=cb_fs_call(&client,filesystem,CB_FILESYSTEM_OPEN,path,c->mode,0);if(status)break;close_handle(&c->output);cb_copy(&c->output,cb_result(&client),8);out.writer=0;}}
    if(!status)for(struct command *c=first;c;c=c->next){status=launch(c);if(status)break;}
    for(struct command *c=first;c;c=c->next){if(c->input==input.reader)input.reader=0;if(c->output==out.writer)out.writer=0;close_handle(&c->input);close_handle(&c->output);}close_handle(&input.reader);close_handle(&out.writer);close_handle(&err.writer);terminal_error_writer=0;
    if(status){error(status);close_handle(&input.writer);close_handle(&out.reader);close_handle(&err.reader);for(struct command *c=first;c;c=c->next)if(c->process){struct cb_application_ref ref={c->process};cb_invoke(&client,applications,(struct cb_interface_id)CB_APPLICATIONS_INTERFACE_ID,CB_APPLICATION_STOP,&ref,sizeof(ref),0);}}else {
        reset_line();size_t pending=0,sent=0;char input_line[sizeof(line)+1];int output_eof=0,error_eof=0;
        input_begin();for(;;){reap_orphans();unsigned running=0;for(struct command *c=first;c;c=c->next)if(!complete(c))++running;
            if(!output_eof){cb_status_t s=pump(out.reader,&output_eof);if(s){error(s);output_eof=1;}}if(!error_eof){cb_status_t s=pump(err.reader,&error_eof);if(s){error(s);error_eof=1;}}if(!running && output_eof && error_eof)break;
            if(pending && input.writer){size_t n=0;cb_status_t s=cb_stream_write(&client,input.writer,input_line+sent,pending-sent,&n,CB_STREAM_NONBLOCK);if(!s){sent+=n;if(sent==pending)pending=sent=0;}else if(s!=CB_WOULD_BLOCK){close_handle(&input.writer);pending=sent=0;}}
            struct cb_keyboard_event key;if(!pending && next_key(&key)){int action=edit(&key,0);if(action==2){close_handle(&input.writer);}else if(action==1){if(input.writer){cb_copy(input_line,line,editor.length);input_line[editor.length]='\n';pending=editor.length+1;sent=0;reset_line();}else reset_line();}}
            if(!input_suspended && !input_active)input_begin();
            wait_activity(first,output_eof?0:out.reader,error_eof?0:err.reader,pending?input.writer:0);
        }
        input_pause();close_handle(&input.writer);close_handle(&out.reader);close_handle(&err.reader);
    }
cleanup:while(first){struct command *next=first->next;release(first->argv);first->argv=NULL;if(first->process){first->next=orphans;orphans=first;}else {release(first->completion);release(first);}first=next;}release(storage);release(tokens);reset_line();
}
/* Local string comparison avoids a libc dependency. */
int strcmp(const char *a,const char *b){while(*a && *a==*b){++a;++b;}return (unsigned char)*a-(unsigned char)*b;}
__attribute__((noreturn)) void _start(const struct cb_startup *packet){struct cb_application_start start;CB_REQUIRE(cb_application_start(packet,&start));CB_REQUIRE(start.header.cwd_bytes<sizeof(cwd));cb_copy(cwd,start.cwd,start.header.cwd_bytes);cwd[start.header.cwd_bytes]=0;memory=cb_acquire(CB_MEMORY_NAME,(struct cb_interface_id)CB_MEMORY_INTERFACE_ID);keyboard=cb_acquire(CB_KEYBOARD_NAME,(struct cb_interface_id)CB_KEYBOARD_INTERFACE_ID);CB_REQUIRE(cb_invoke(&client,keyboard,(struct cb_interface_id)CB_KEYBOARD_INTERFACE_ID,CB_KEYBOARD_FOCUS_CLAIM,NULL,0,0)==CB_OK);struct cb_keyboard_focus focus;cb_copy(&focus,cb_result(&client),sizeof(focus));keyread.provider_epoch=focus.provider_epoch;keyread.after_sequence=focus.next_sequence;focus_generation=focus.generation;text=cb_acquire(CB_TEXT_NAME,(struct cb_interface_id)CB_TEXT_INTERFACE_ID);applications=cb_acquire(CB_APPLICATIONS_NAME,(struct cb_interface_id)CB_APPLICATIONS_INTERFACE_ID);streams=cb_acquire(CB_STREAM_NAME,(struct cb_interface_id)CB_STREAM_INTERFACE_ID);filesystem=cb_acquire(CB_FILESYSTEM_NAME,(struct cb_interface_id)CB_FILESYSTEM_INTERFACE_ID);CB_REQUIRE(cb_invoke(&client,text,(struct cb_interface_id)CB_TEXT_INTERFACE_ID,CB_TEXT_INFO,NULL,0,0)==CB_OK);struct cb_text_info dimensions;cb_copy(&dimensions,cb_result(&client),sizeof(dimensions));columns=dimensions.columns;paint=allocate((size_t)columns*4+1);prompt();for(;;){reap_orphans();struct cb_keyboard_event key;if(next_key(&key)){int action=edit(&key,1);if(action==2){input_pause();struct cb_abandon_result result;if(keyboard_client.pending.serial)CB_REQUIRE(cb_abandon(&keyboard_client,&result)==CB_OK);cb_exit(0);}if(action==1){remember();execute();prompt();}}else wait_activity(NULL,0,0,0);}}
