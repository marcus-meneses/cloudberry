/* Policy consumes the same pinned installation snapshot as process launch. */
static int path_under(const char *path,const char *base){size_t n=length(base);return length(path)>=n && cb_install_equal(path,base,n) && (!path[n] || path[n]=='/');}
static cb_status_t installation_data_path(uint64_t pid,const struct installation_ref *ref,char out[4096]){
    const struct cb_install_record *r=&ref->record;if(!(r->flags&CB_INSTALL_APPLICATION))return CB_DENIED;
    kernel_surface_call home=kernel_surface_find(runtime.hub,"process.home");struct kernel_surface_result got=home?home(pid,(uintptr_t)out,4096,0,0,0):module_result(0,0);if(got.status)return CB_UNAVAILABLE;size_t n=got.value;
    const unsigned char *name=ref->name;size_t bytes=r->name_bytes;
    if(ref->snapshot && registry_find(ref->snapshot,ref->owner,r->installation)){const unsigned char *path=ref->name+r->name_bytes;size_t last=0;for(size_t i=0;i<r->path_bytes;++i)if(path[i]=='/')last=i+1;name=path+last;bytes=r->path_bytes-last;}
    if(n>4095 || bytes>4095-n || 12>4095-n-bytes)return CB_BAD_REQUEST;
    cb_install_copy(out+n,"/_apps/",7);cb_install_copy(out+n+7,name,bytes);cb_install_copy(out+n+7+bytes,".data",6);return CB_OK;
}
static cb_status_t application_data_path(uint64_t pid,char out[4096]){struct installation_ref ref;if(!installation_process(pid,&ref))return CB_DENIED;cb_status_t status=installation_data_path(pid,&ref,out);installation_drop(&ref);return status;}
static cb_status_t installation_access(uint64_t pid,const char *path,uint64_t op,struct installation_ref *ref){
    struct cb_install_record record=ref->record;const unsigned char *name=ref->name;
    int writes=op==FS_WRITE || op==FS_PREPARE || op==FS_REMOVE || op==FS_RMDIR || op==FS_MKDIR || op==FS_CHMOD || op==FS_CHOWN;
    int identity=record.flags==CB_INSTALL_PROVIDER && record.name_bytes==8 && cb_install_equal(name,"identity",8);
    int installer=record.flags==CB_INSTALL_PROVIDER && record.name_bytes==9 && cb_install_equal(name,"installer",9);
    if(path_under(path,"/system/config/identity"))return identity?CB_OK:CB_DENIED;
    if(path_under(path,"/system/config/installations"))return installer?CB_OK:CB_DENIED;
    if(installer)return path_under(path,"/boot") && writes?CB_DENIED:CB_OK;
    char own[4096];cb_status_t own_status=installation_data_path(pid,ref,own);
    if(writes && (path_under(path,"/system") || path_under(path,"/boot")) && (own_status || !path_under(path,own)))return CB_DENIED;
    kernel_surface_call credentials=kernel_surface_find(runtime.hub,"process.credentials");struct cb_credentials who;if(!credentials || credentials(pid,0,(uintptr_t)&who,sizeof(who),0,0).status)return CB_DENIED;
    if(!who.uid)return CB_OK;
    /* A launcher may execute another registered app visible to this UID,
     * without acquiring read access to that app's private data or sidecars. */
    if(op==FS_EXEC){struct registry_snapshot *snapshot=registry_pin();int allowed=0;
        if(snapshot)for(size_t at=32;at<snapshot->bytes;){const unsigned char *p=snapshot->data+at;struct cb_install_record target;cb_install_copy(&target,p+16,64);
            uint64_t owner=cb_reg_u64(p);if((owner==who.uid || owner==CB_REGISTRY_SYSTEM) && target.path_bytes==length(path) && cb_install_equal(p+80+target.name_bytes,path,target.path_bytes)){allowed=1;break;}at+=16+target.bytes;
            }registry_drop(snapshot);if(allowed)return CB_OK;
        }
    if(path_under(path,"/home")){const char *tail=path+5;if(*tail=='/')++tail;while(*tail && *tail!='/')++tail;if(path_under(tail,"/_apps")){
        const char *entry=tail+6;if(!*entry || (length(entry)==7 && cb_install_equal(entry,"/_index",7)))return writes?CB_DENIED:CB_OK;
        if(!own_status && path_under(path,own)){if((op==FS_REMOVE || op==FS_RMDIR) && length(path)==length(own))return CB_DENIED;return CB_OK;}
        if(!writes){char executable[4096];if(record.path_bytes<sizeof(executable)-15){cb_install_copy(executable,name+record.name_bytes,record.path_bytes);executable[record.path_bytes]=0;if(path_under(path,executable) && length(path)==record.path_bytes)return CB_OK;cb_install_copy(executable+record.path_bytes,".resources",11);if(path_under(path,executable))return CB_OK;cb_install_copy(executable+record.path_bytes,".manifest.json",15);if(length(path)==record.path_bytes+14 && path_under(path,executable))return CB_OK;}}
        return CB_DENIED;
    }}return CB_OK;
}
struct kernel_surface_result applications_surface_access(MODULE_ARGS){MODULE_UNUSED();if(!runtime.hub || !a || !b)return module_result(CB_DENIED,1);struct installation_ref ref;if(!installation_process(a,&ref))return module_result(CB_DENIED,1);cb_status_t status=installation_access(a,(const char *)b,c,&ref);installation_drop(&ref);return module_result(status,1);}
