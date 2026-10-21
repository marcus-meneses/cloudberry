/* Identity authority: IO authenticates this provider's installed PID. No client
 * can select another process as the target of an identity transition. */
static uint64_t auth_after;
static cb_status_t caller_credentials(uint64_t pid,struct cb_credentials *who,int *admin){
    cb_status_t status=cb_invoke(&client,io,(struct cb_interface_id)CB_IO_INTERFACE_ID,CB_IO_CREDENTIALS,&pid,8,0);if(status)return status;
    struct cb_reply_header reply;cb_copy(&reply,client.result,sizeof(reply));if(reply.result_bytes<sizeof(*who))return CB_RESOURCE_FAILURE;cb_copy(who,cb_result(&client),sizeof(*who));
    if(who->groups>(reply.result_bytes-sizeof(*who))/8)return CB_RESOURCE_FAILURE;
    *admin=!who->uid || who->gid==1;for(size_t i=0;i<who->groups;++i){uint64_t gid;cb_copy(&gid,(const unsigned char *)cb_result(&client)+sizeof(*who)+i*8,8);if(gid==1)*admin=1;}return CB_OK;
}
static uint64_t auth_now(void){if(cb_invoke(&client,io,(struct cb_interface_id)CB_IO_INTERFACE_ID,CB_IO_NOW_NS,NULL,0,0))return 0;uint64_t n;cb_copy(&n,cb_result(&client),8);return n;}
static int password_matches(uint64_t uid,const unsigned char *secret,size_t bytes){
    const unsigned char *record=id_find(&database.files[2],uid);const unsigned char *fallback=id_find(&database.files[2],0);if(!fallback)return 0;
    const unsigned char *r=record?record:fallback;unsigned char hash[32];int ok=id_password_derive(secret,bytes,r+32,id_u32(r+16),hash);int match=id_password_equal(hash,r+64);id_wipe(hash,sizeof(hash));return ok && match && record;
}
static cb_status_t authenticate(uint64_t pid,const struct cb_identity_password *r,const unsigned char *payload){
    if(r->old_bytes || !id_password_valid(payload,r->password_bytes))return CB_BAD_REQUEST;
    uint64_t now=auth_now();if(now<auth_after)return CB_DENIED;
    const unsigned char *u=id_find(&database.files[0],r->uid);int match=password_matches(r->uid,payload,r->password_bytes);
    if(!match || !u || id_u32(u+4)!=1){now=auth_now();auth_after=now>UINT64_MAX-1000000000?UINT64_MAX:now+1000000000;return CB_DENIED;}
    size_t groups=0;for(size_t at=ID_HEADER;at<database.files[1].bytes;at+=id_u32(database.files[1].data+at)){const unsigned char *g=database.files[1].data+at;for(size_t i=0;i<id_u32(g+20);++i)if(id_u64(g+32+id_u32(g+16)+i*8)==r->uid){++groups;break;}}
    if(groups>(CB_FRAME_LIMIT-1024-8-sizeof(struct cb_credentials))/8)return CB_BUFFER_TOO_SMALL;
    size_t bytes=8+sizeof(struct cb_credentials)+groups*8;unsigned char *packet=allocate(NULL,bytes);if(!packet)return CB_RESOURCE_FAILURE;cb_copy(packet,&pid,8);struct cb_credentials who={.uid=r->uid,.gid=id_u64(u+16),.groups=groups};cb_copy(packet+8,&who,sizeof(who));size_t n=0;
    for(size_t at=ID_HEADER;at<database.files[1].bytes;at+=id_u32(database.files[1].data+at)){const unsigned char *g=database.files[1].data+at;for(size_t i=0;i<id_u32(g+20);++i)if(id_u64(g+32+id_u32(g+16)+i*8)==r->uid){uint64_t gid=id_u64(g+8);cb_copy(packet+8+sizeof(who)+n++*8,&gid,8);break;}}
    cb_status_t status=cb_invoke(&client,io,(struct cb_interface_id)CB_IO_INTERFACE_ID,CB_IO_IDENTITY_SET,packet,bytes,0);release(NULL,packet);return status;
}
static cb_status_t change_password(uint64_t pid,const struct cb_credentials *who,int admin,const struct cb_identity_password *r,const unsigned char *payload){
    if(!id_password_valid(payload+r->old_bytes,r->password_bytes) || (r->old_bytes && !id_password_valid(payload,r->old_bytes)))return CB_BAD_REQUEST;
    if(!id_find(&database.files[0],r->uid))return CB_NOT_FOUND;
    if(!r->uid && who->uid)return CB_DENIED;
    if(!admin){if(who->uid!=r->uid || !r->old_bytes)return CB_DENIED;uint64_t now=auth_now();if(now<auth_after)return CB_DENIED;if(!password_matches(r->uid,payload,r->old_bytes)){auth_after=now>UINT64_MAX-1000000000?UINT64_MAX:now+1000000000;return CB_DENIED;}}
    for(struct transaction *t=transactions;t;t=t->next)if(t->owner==pid)return CB_BUSY;
    struct cb_identity_credential record={.uid=r->uid,.algorithm=1,.rounds=ID_KDF_MIN_ROUNDS};uint64_t count=32;cb_status_t status=cb_invoke(&client,io,(struct cb_interface_id)CB_IO_INTERFACE_ID,CB_IO_RANDOM,&count,8,0);if(status)return status;cb_copy(record.salt,cb_result(&client),32);
    if(!id_password_derive(payload+r->old_bytes,r->password_bytes,record.salt,record.rounds,record.hash))return CB_RESOURCE_FAILURE;
    struct cb_identity_begin request={.generation=id_u64(database.files[0].data+24)};uint64_t token;status=begin(pid,&request,&token);if(!status){struct transaction *t=find(pid,token);status=credential(t,&record);if(!status)status=id_store_commit(&database,t->files);dispose(t);}id_wipe(&record,sizeof(record));return status;
}
