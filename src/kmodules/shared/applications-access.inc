/* Installation policy, using only copied kernel-owned process/catalog identity.
 * The filesystem manager supplies a canonical path in the mounted namespace. */
static int path_under(const char *path,const char *base){size_t n=length(base);return length(path)>=n && cb_install_equal(path,base,n) && (!path[n] || path[n]=='/');}
static int caller_installation(uint64_t pid,struct cb_install_record *record,const unsigned char **name,uint8_t identity[16]){kernel_surface_call get=kernel_surface_find(runtime.hub,"handles.identity");if(!runtime.catalog || !get || get(pid,(uintptr_t)identity,16,0,0,0).status)return 0;*name=cb_install_find(runtime.catalog,NULL,0,identity,record);return *name!=NULL;}
static cb_status_t application_data_path(uint64_t pid,char out[4096]){
    struct cb_install_record record;const unsigned char *name;uint8_t identity[16];if(!caller_installation(pid,&record,&name,identity) || !(record.flags&CB_INSTALL_APPLICATION))return CB_DENIED;
    kernel_surface_call home=kernel_surface_find(runtime.hub,"process.home");struct kernel_surface_result got=home?home(pid,(uintptr_t)out,4096,0,0,0):module_result(0,0);if(got.status)return CB_UNAVAILABLE;size_t n=got.value;
    if(n>4095 || record.name_bytes>4095-n || 12>4095-n-record.name_bytes)return CB_BAD_REQUEST;
    cb_install_copy(out+n,"/_apps/",7);cb_install_copy(out+n+7,name,record.name_bytes);cb_install_copy(out+n+7+record.name_bytes,".data",6);return CB_OK;
}
struct kernel_surface_result applications_surface_access(MODULE_ARGS){
    MODULE_UNUSED();if(!runtime.hub || !a || !b)return module_result(CB_DENIED,1);const char *path=(const void *)b;struct cb_install_record record;const unsigned char *name;uint8_t identity[16];if(!caller_installation(a,&record,&name,identity))return module_result(CB_DENIED,1);
    int writes=c==FS_WRITE || c==FS_PREPARE || c==FS_REMOVE || c==FS_MKDIR || c==FS_CHMOD || c==FS_CHOWN;
    if(path_under(path,"/system/config/identity"))return module_result(record.flags==CB_INSTALL_PROVIDER && record.name_bytes==8 && cb_install_equal(name,"identity",8)?CB_OK:CB_DENIED,1);
    if(writes && (path_under(path,"/system") || path_under(path,"/boot")) && !applications_data(identity,path))return module_result(CB_DENIED,1);
    /* The privileged identity installation owns explicit home provisioning.
     * Root retains its established administrative access; ordinary UID peers
     * must still carry the installation matching their private container. */
    kernel_surface_call credentials=kernel_surface_find(runtime.hub,"process.credentials");struct cb_credentials who;if(!credentials || credentials(a,0,(uintptr_t)&who,sizeof(who),0,0).status)return module_result(CB_DENIED,1);
    if(!who.uid)return module_result(CB_OK,1);
    if(path_under(path,"/home")){
        const char *tail=path+5;if(*tail=='/')++tail;while(*tail && *tail!='/')++tail;
        if(path_under(tail,"/_apps")){
            const char *entry=tail+6;
            if(!*entry || (length(entry)==7 && cb_install_equal(entry,"/_index",7)))return module_result(writes?CB_DENIED:CB_OK,1);
            char own[4096];cb_status_t status=application_data_path(a,own);if(status)return module_result(status,1);
            if(!path_under(path,own))return module_result(CB_DENIED,1);
            if(c==FS_REMOVE && length(path)==length(own))return module_result(CB_DENIED,1);
        }
    }
    return module_result(CB_OK,1);
}
