/* Explicit management operation, never invoked during provider startup. The
 * parent remains root-owned 0700 until every child is assigned to the account.
 * A failed operation can leave a private staging tree; no ownership takeover. */
static cb_status_t home_metadata(const char *path,struct cb_filesystem_metadata *m){cb_status_t s=cb_fs_call(&client,filesystem,CB_FILESYSTEM_METADATA,path,0,0);if(!s)cb_copy(m,cb_result(&client),sizeof(*m));return s;}
static cb_status_t home_apps(char path[4096],size_t base,uint64_t uid,uint64_t gid){
    size_t capacity=1024,used=0;unsigned char *index=allocate(NULL,capacity);if(!index)return CB_RESOURCE_FAILURE;
    const char intro[]="{\"version\":1,\"entries\":[";cb_copy(index,intro,sizeof(intro)-1);used=sizeof(intro)-1;
    struct cb_registry_request query={.operation=CB_REGISTRY_LIST,.flags=1,.actor=uid};unsigned entries=0;cb_status_t status=CB_OK;
    for(;;){status=cb_invoke(&client,io,(struct cb_interface_id)CB_IO_INTERFACE_ID,CB_IO_REGISTRY,&query,sizeof(query),0);if(status)break;struct cb_reply_header reply;cb_copy(&reply,client.result,sizeof(reply));if(!reply.result_bytes)break;
        struct cb_registry_entry e;cb_copy(&e,cb_result(&client),sizeof(e));if(e.name_bytes>220 || reply.result_bytes!=sizeof(e)+e.name_bytes){status=CB_BAD_REQUEST;break;}query.token=e.generation;query.offset=e.cursor;
        char name[221],filename[256],executable[4096];cb_copy(name,(unsigned char *)cb_result(&client)+sizeof(e),e.name_bytes);name[e.name_bytes]=0;size_t fn=0;
        static const char hex[]="0123456789abcdef";if(e.dynamic){filename[fn++]='_';for(unsigned i=0;i<16;++i){filename[fn++]=hex[e.installation[i]>>4];filename[fn++]=hex[e.installation[i]&15];}filename[fn++]='.';}cb_copy(filename+fn,name,e.name_bytes+1);fn+=e.name_bytes;
        if(base+7+fn+6>sizeof(executable)){status=CB_BAD_REQUEST;break;}cb_copy(path+base,"/_apps/",7);cb_copy(path+base+7,filename,fn);cb_copy(path+base+7+fn,".data",6);
        struct cb_filesystem_metadata m;status=home_metadata(path,&m);if(status==CB_NOT_FOUND){status=cb_fs_call(&client,filesystem,CB_FILESYSTEM_MKDIR,path,0,0);if(!status)status=home_metadata(path,&m);}if(status)break;if(!m.directory || (m.uid && m.uid!=uid) || (m.gid && m.gid!=gid)){status=CB_DENIED;break;}status=cb_fs_call(&client,filesystem,CB_FILESYSTEM_CHMOD,path,0700,0);if(!status)status=cb_fs_chown(&client,filesystem,path,uid,gid);if(status)break;
        if(e.owner==CB_REGISTRY_SYSTEM){cb_copy(executable,"/system/apps/",13);cb_copy(executable+13,filename,fn+1);}else{cb_copy(executable,path,base+7+fn);executable[base+7+fn]=0;}
        size_t need=used+e.name_bytes+cb_length(executable)+100;if(need>capacity){size_t next=capacity;while(next<need && next<=SIZE_MAX/2)next*=2;if(next<need){status=CB_RESOURCE_FAILURE;break;}unsigned char *larger=allocate(NULL,next);if(!larger){status=CB_RESOURCE_FAILURE;break;}cb_copy(larger,index,used);release(NULL,index);index=larger;capacity=next;}
        if(entries++)index[used++]=',';
        const char prefix[]="{\"app_id\":\"";cb_copy(index+used,prefix,sizeof(prefix)-1);used+=sizeof(prefix)-1;for(unsigned j=0;j<16;++j){index[used++]=hex[e.installation[j]>>4];index[used++]=hex[e.installation[j]&15];}
        const char middle[]="\",\"name\":\"";cb_copy(index+used,middle,sizeof(middle)-1);used+=sizeof(middle)-1;cb_copy(index+used,name,e.name_bytes);used+=e.name_bytes;
        const char install[]="\",\"installation\":\"";cb_copy(index+used,install,sizeof(install)-1);used+=sizeof(install)-1;size_t n=cb_length(executable);cb_copy(index+used,executable,n);used+=n;cb_copy(index+used,"\"}",2);used+=2;
    }
    if(!status){cb_copy(index+used,"]}\n",3);used+=3;cb_copy(path+base,"/_apps/_index",14);status=write_file(NULL,path,index,used);if(!status)status=cb_fs_chown(&client,filesystem,path,uid,gid);}
    release(NULL,index);path[base]=0;return status;
}
static cb_status_t home(uint64_t uid,const struct cb_credentials *who,int admin,size_t *result){
    const unsigned char *user=id_find(&database.files[0],uid);if(!user)return CB_NOT_FOUND;
    if(uid!=who->uid && !admin)return CB_DENIED;
    char path[4096];size_t at=0;if(!uid){cb_copy(output,"/system/root",12);*result=12;return CB_OK;}
    size_t name=id_u32(user+24);if(name>sizeof(path)-32)return CB_BAD_REQUEST;
    cb_copy(path,"/home/",6);cb_copy(path+6,user+32,name);at=6+name;path[at]=0;uint64_t gid=id_u64(user+16);if(uid>UINT32_MAX || gid>UINT32_MAX)return CB_DENIED;
    struct cb_filesystem_metadata m;cb_status_t s=home_metadata(path,&m);
    if(!s && m.directory && m.uid==uid && m.gid==gid){cb_copy(output,path,at);*result=at;return CB_OK;}
    if(!admin)return s?s:CB_DENIED;
    if(s==CB_NOT_FOUND){s=cb_fs_call(&client,filesystem,CB_FILESYSTEM_MKDIR,path,0,0);if(s)return s;s=home_metadata(path,&m);}
    if(s)return s;
    if(!m.directory || m.uid || m.gid || m.mode!=0700)return CB_DENIED;
    static const char *children[]={"/_config","/_apps","/documents","/downloads","/desktop"};
    for(unsigned i=0;i<sizeof(children)/sizeof(children[0]);++i){size_t n=cb_length(children[i]);cb_copy(path+at,children[i],n+1);s=home_metadata(path,&m);if(s==CB_NOT_FOUND){s=cb_fs_call(&client,filesystem,CB_FILESYSTEM_MKDIR,path,0,0);if(!s)s=home_metadata(path,&m);}if(s)return s;
        if(!m.directory || (m.uid!=0 && m.uid!=uid) || (m.gid!=0 && m.gid!=gid))return CB_DENIED;
        s=cb_fs_call(&client,filesystem,CB_FILESYSTEM_CHMOD,path,0700,0);if(!s)s=cb_fs_chown(&client,filesystem,path,uid,gid);if(s)return s;
    }
    path[at]=0;s=home_apps(path,at,uid,gid);if(s)return s;
    path[at]=0;s=cb_fs_chown(&client,filesystem,path,uid,gid);if(s)return s;cb_copy(output,path,at);*result=at;return CB_OK;
}
