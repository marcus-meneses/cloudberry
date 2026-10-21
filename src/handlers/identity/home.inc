/* Explicit management operation, never invoked during provider startup. The
 * parent remains root-owned 0700 until every child is assigned to the account.
 * A failed operation can leave a private staging tree; no ownership takeover. */
static cb_status_t home_metadata(const char *path,struct cb_filesystem_metadata *m){cb_status_t s=cb_fs_call(&client,filesystem,CB_FILESYSTEM_METADATA,path,0,0);if(!s)cb_copy(m,cb_result(&client),sizeof(*m));return s;}
static cb_status_t home_apps(char path[4096],size_t base,uint64_t uid,uint64_t gid){
    struct cb_install_catalog header;cb_copy(&header,catalog,sizeof(header));if(header.bytes>(SIZE_MAX-64)/3)return CB_RESOURCE_FAILURE;
    unsigned char *index=allocate(NULL,header.bytes*3+64);if(!index)return CB_RESOURCE_FAILURE;size_t used=0;const char intro[]="{\"version\":1,\"entries\":[";cb_copy(index,intro,sizeof(intro)-1);used=sizeof(intro)-1;size_t cursor=sizeof(header);unsigned entries=0;cb_status_t status=CB_OK;
    for(uint64_t i=0;i<header.count;++i){struct cb_install_record r;cb_copy(&r,catalog+cursor,sizeof(r));const unsigned char *name=catalog+cursor+sizeof(r);cursor+=r.bytes;if(!(r.flags&CB_INSTALL_APPLICATION))continue;
        if(r.name_bytes>4096-base-14){status=CB_BAD_REQUEST;break;}cb_copy(path+base,"/_apps/",7);cb_copy(path+base+7,name,r.name_bytes);cb_copy(path+base+7+r.name_bytes,".data",6);struct cb_filesystem_metadata m;status=home_metadata(path,&m);if(status==CB_NOT_FOUND){status=cb_fs_call(&client,filesystem,CB_FILESYSTEM_MKDIR,path,0,0);if(!status)status=home_metadata(path,&m);}if(status)break;if(!m.directory || (m.uid && m.uid!=uid) || (m.gid && m.gid!=gid)){status=CB_DENIED;break;}status=cb_fs_call(&client,filesystem,CB_FILESYSTEM_CHMOD,path,0700,0);if(!status)status=cb_fs_chown(&client,filesystem,path,uid,gid);if(status)break;
        if(entries++)index[used++]=',';
        const char prefix[]="{\"app_id\":\"";cb_copy(index+used,prefix,sizeof(prefix)-1);used+=sizeof(prefix)-1;static const char hex[]="0123456789abcdef";for(unsigned j=0;j<16;++j){index[used++]=hex[r.installation[j]>>4];index[used++]=hex[r.installation[j]&15];}
        const char middle[]="\",\"name\":\"";cb_copy(index+used,middle,sizeof(middle)-1);used+=sizeof(middle)-1;cb_copy(index+used,name,r.name_bytes);used+=r.name_bytes;const char installation[]="\",\"installation\":\"";cb_copy(index+used,installation,sizeof(installation)-1);used+=sizeof(installation)-1;cb_copy(index+used,name+r.name_bytes,r.path_bytes);used+=r.path_bytes;cb_copy(index+used,"\"}",2);used+=2;
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
