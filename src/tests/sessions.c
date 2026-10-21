/* Exercise provider policy against failed kernel closes, then successful retries. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define _start sessions_entry
#include "../handlers/sessions/sessions.c"
#undef _start
static cb_status_t failure;
static unsigned closes,uid_closes;
cb_status_t cb_system_invoke(const void *input,uint64_t bytes,void *output,uint64_t capacity,uint64_t *actual){
    struct cb_call_header c;memcpy(&c,input,sizeof(c));assert(bytes==sizeof(c)+c.payload_bytes && c.handle==1);
    struct cb_reply_header reply={.version={sizeof(reply),1,0}};struct cb_session_status state={0};
    if(c.operation==CB_IO_SESSION){struct cb_io_session r;memcpy(&r,(const unsigned char *)input+sizeof(c),sizeof(r));
        if(r.operation==2){++closes;reply.status=failure;}
        else if(r.operation==4){++uid_closes;reply.status=failure;}
        else {assert(r.operation==3 || r.operation==5);state=(struct cb_session_status){.members=1,.closing=1};state.session.uid=r.process;reply.result_bytes=sizeof(state);}
    }else {assert(c.operation==CB_IO_CREDENTIALS);uint64_t pid;memcpy(&pid,(const unsigned char *)input+sizeof(c),8);struct cb_credentials who={.uid=pid,.gid=pid==7?1:2};assert(capacity>=sizeof(reply)+sizeof(who));reply.result_bytes=sizeof(who);memcpy(output,&reply,sizeof(reply));memcpy((unsigned char *)output+sizeof(reply),&who,sizeof(who));*actual=sizeof(reply)+sizeof(who);return CB_OK;}
    assert(capacity>=sizeof(reply)+reply.result_bytes);memcpy(output,&reply,sizeof(reply));if(reply.result_bytes)memcpy((unsigned char *)output+sizeof(reply),&state,sizeof(state));*actual=sizeof(reply)+reply.result_bytes;return reply.status;
}
cb_status_t cb_system_reply(const void *data,uint64_t bytes){struct cb_reply_header h;memcpy(&h,data,sizeof(h));assert(h.status==CB_UNAVAILABLE && h.result_bytes==sizeof(struct cb_session_status) && bytes==sizeof(h)+h.result_bytes);struct cb_session_status report;memcpy(&report,(const unsigned char *)data+sizeof(h),sizeof(report));assert(report.close_error==CB_UNAVAILABLE);return CB_OK;}
cb_status_t cb_system_request_release(struct cb_request_id id){(void)id;abort();}
__attribute__((noreturn)) void cb_exit(uint64_t code){(void)code;abort();}
int main(void){
    io=1;struct session parent={.info={.id=10,.uid=42}},child={.info={.id=11,.parent=10,.uid=99}},other={.info={.id=12,.uid=90}};sessions=&parent;parent.next=&child;child.next=&other;
    failure=CB_UNAVAILABLE;assert(close_session(10,10)==CB_UNAVAILABLE && closes==2 && parent.closing && child.closing && !other.closing);
    struct cb_session_status status;assert(snapshot(&parent,&status)==CB_OK && status.close_error==CB_UNAVAILABLE && status.running==1);
    assert(snapshot(NULL,&status)==CB_NOT_FOUND && close_session(100,0)==CB_BAD_HANDLE);
    struct cb_delivery_header delivery={0};assert(respond(&delivery,CB_UNAVAILABLE,&status,sizeof(status))==CB_OK);
    failure=CB_OK;assert(close_session(10,10)==CB_OK && !parent.close_error && !child.close_error);assert(snapshot(&parent,&status)==CB_OK && !status.close_error);
    assert(revoke_uid(0,&status)==CB_DENIED && !uid_closes);
    failure=CB_RESOURCE_FAILURE;assert(revoke_uid(42,&status)==CB_RESOURCE_FAILURE && uid_closes==1 && status.close_error==CB_RESOURCE_FAILURE);
    failure=CB_OK;assert(revoke_uid(42,&status)==CB_OK && uid_closes==2 && !status.close_error);
    assert(management(7)==CB_OK && management(8)==CB_DENIED);
    struct cb_session owner={.id=10};size_t result;uint64_t sid=10;
    assert(end_session(8,&owner,&sid,8,&status,&result)==CB_DENIED && !result);
    sid=0;assert(end_session(7,&owner,&sid,8,&status,&result)==CB_DENIED && !result);
    sid=100;assert(end_session(7,&owner,&sid,8,&status,&result)==CB_BAD_HANDLE && !result);
    sid=10;failure=CB_UNAVAILABLE;assert(end_session(7,&owner,&sid,8,&status,&result)==CB_UNAVAILABLE && result==sizeof(status) && status.close_error==CB_UNAVAILABLE);
    failure=CB_OK;assert(end_session(7,&owner,&sid,8,&status,&result)==CB_OK && result==sizeof(status) && !status.close_error);
    assert(end_session(10,&owner,NULL,0,&status,&result)==CB_OK);
    assert(end_session(8,&owner,NULL,0,&status,&result)==CB_DENIED && !result);
    puts("PASS sessions: subtree close errors, missing records, retry, UID authority and protected boot root");
}
