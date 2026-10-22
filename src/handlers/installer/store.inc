#ifndef CB_INSTALL_FAULT
#ifdef CB_INSTALL_CRASH_POINT
static unsigned test_active_operation;
static void installer_test_cut(unsigned point){if(point==CB_INSTALL_CRASH_POINT && test_active_operation==CB_INSTALL_CRASH_OPERATION){struct cb_filesystem_metadata m;if(metadata("/system/config/installations/test-cut.bin",&m)==CB_NOT_FOUND){uint64_t marker=point;CB_REQUIRE(write_file("/system/config/installations/test-cut.bin",&marker,8,CB_FILE_RECREATE)==CB_OK);cb_exit(100+point);}}}
#define CB_INSTALL_FAULT(point) installer_test_cut(point)
#else
#define CB_INSTALL_FAULT(point) ((void)0)
#endif
#endif
#define REGISTRY_PATH "/system/config/installations/registry.bin"
#define JOURNAL_PATH "/system/config/installations/transaction.bin"
static cb_status_t stage_registry_guarded(const unsigned char *data,size_t bytes,uint64_t actor,unsigned restore,uint64_t *token,const struct cb_registry_target *guard){
    cb_status_t s=registry(CB_REGISTRY_VERSION,0,0,0,0,0,NULL,0);
    if(s)return s;
    uint64_t generation;
    cb_copy(&generation,cb_result(&client),8);
    s=registry(CB_REGISTRY_BEGIN,actor,generation,0,bytes,restore,NULL,0);
    if(s)return s;
    cb_copy(token,cb_result(&client),8);
    for(size_t at=0;at<bytes;){size_t n=bytes-at;
        if(n>8192)n=8192;
        s=registry(CB_REGISTRY_STAGE,0,*token,at,0,0,data+at,n);
        if(s)goto abort;
        at+=n;
        }
    s=registry(CB_REGISTRY_PREPARE,0,*token,0,0,guard?1:0,guard,guard?sizeof(*guard):0);
    if(!s)return CB_OK;
    abort:(void)registry(CB_REGISTRY_ABORT,0,*token,0,0,0,NULL,0);
    return s;
    }
static cb_status_t stage_registry(const unsigned char *data,size_t bytes,uint64_t actor,unsigned restore,uint64_t *token){return stage_registry_guarded(data,bytes,actor,restore,token,NULL);}
static cb_status_t stage_publication(const struct action *a,uint64_t actor,uint64_t *token,const struct cb_registry_target *guard){
    unsigned char *live=allocate(a->next_bytes);if(!live)return CB_RESOURCE_FAILURE;cb_copy(live,a->next,32);size_t used=32;uint64_t count=0;
    for(size_t at=32;at<a->next_bytes;){struct cb_install_record r;cb_copy(&r,a->next+at+16,64);uint64_t owner=cb_reg_u64(a->next+at);int target=owner==a->owner && cb_install_equal(r.installation,a->identity,16);
        if(!unavailable(owner,r.installation) || (target && a->operation==4)){cb_copy(live+used,a->next+at,16+r.bytes);used+=16+r.bytes;++count;}at+=16+r.bytes;}
    cb_reg_put(live+8,used);cb_reg_put(live+24,count);cb_status_t s=stage_registry_guarded(live,used,actor,0,token,guard);release(live);return s;
}
static cb_status_t save_registry(const unsigned char *data,size_t bytes){if(bytes>SIZE_MAX-64)return CB_RESOURCE_FAILURE;
    unsigned char *file=allocate(bytes+64);
    if(!file)return CB_RESOURCE_FAILURE;
    cb_copy(file,data,bytes);
    crypto_sha512(file+bytes,data,bytes);
    cb_status_t s=cb_fs_call(&client,filesystem,CB_FILESYSTEM_OPEN,REGISTRY_PATH,CB_FILE_RECREATE,0);
    if(!s){cb_handle_t handle;cb_copy(&handle,cb_result(&client),8);CB_INSTALL_FAULT(6);s=cb_write_all(&client,handle,file,bytes+64);cb_status_t closed=cb_system_release(handle);if(!s)s=closed;}
    release(file);
    return s;
    }
static void package_path(const struct action *a,char path[160]){static const char prefix[]="/system/config/installations/packages/",hex[]="0123456789abcdef";
    cb_copy(path,prefix,sizeof(prefix)-1);
    size_t at=sizeof(prefix)-1;
    for(int i=15;i>=0;--i)path[at++]=hex[(a->owner>>(i*4))&15];
    path[at++]='-';
    const unsigned char *identity=a->bundle.data?a->bundle.data+64:a->identity;
    for(unsigned i=0;i<16;++i){path[at++]=hex[identity[i]>>4];path[at++]=hex[identity[i]&15];}cb_copy(path+at,".cbp",5);
    }
/* Keep executable/manifest inodes and replace their contents under the launch
 * gate. Only the resource tree needs removal to drop obsolete resource files. */
static cb_status_t replace_artifacts(struct action *a){char path[4096];cb_status_t s=suffix(path,a->base,".resources");if(!s)s=remove_tree(path);return s?s:write_artifacts(a);}
static cb_status_t restore_previous(struct action *a){
    struct blob copy=a->previous;struct action prior=*a;prior.plans=NULL;cb_status_t s=verify(&copy,&prior.bundle);if(s)return s;
    s=replace_artifacts(&prior);if(!s){char path[160];package_path(&prior,path);s=write_file(path,prior.bundle.data,prior.bundle.bytes,CB_FILE_RECREATE);}
    for(struct data_plan *p=a->plans;!s && p;p=p->next)if(!p->existed){char path[4096];s=data_path(path,p->home,a->base);if(!s)s=remove_tree(path);}
    return s;
}
static cb_status_t intent(struct action *a,struct blob *out){
    size_t plans=0,count=0;
    for(struct data_plan *p=a->plans;p;p=p->next){size_t n=cb_length(p->home);
        if(plans>SIZE_MAX-32-n)return CB_RESOURCE_FAILURE;
        plans+=32+n;
        ++count;
        }
    size_t base=cb_length(a->base),header=(a->operation==3 || a->operation==5)?72:64,bytes=header;
    if(!cb_frame_fits(bytes,base,plans,SIZE_MAX) || a->next_bytes>SIZE_MAX-bytes-base-plans || a->bundle.bytes>SIZE_MAX-bytes-base-plans-a->next_bytes || 64>SIZE_MAX-bytes-base-plans-a->next_bytes-a->bundle.bytes)return CB_RESOURCE_FAILURE;
    if(a->previous.bytes>SIZE_MAX-bytes-base-plans-a->next_bytes-a->bundle.bytes-64)return CB_RESOURCE_FAILURE;
    bytes+=base+plans+a->next_bytes+a->bundle.bytes+a->previous.bytes+64;
    unsigned char *data=allocate(bytes);
    if(!data)return CB_RESOURCE_FAILURE;
    cb_copy(data,a->operation==3?"CBINS002":a->operation==5?"CBINS003":"CBINS001",8);
    cb_reg_put(data+8,a->operation);
    cb_reg_put(data+16,a->owner);
    cb_reg_put(data+24,a->options&(CB_INSTALL_REUSE_DATA|CB_INSTALL_DELETE_DATA|CB_INSTALL_ALLOW_DOWNGRADE));
    cb_reg_put(data+32,base);
    cb_reg_put(data+40,plans);
    cb_reg_put(data+48,a->next_bytes);
    cb_reg_put(data+56,a->bundle.bytes);
    if(header==72)cb_reg_put(data+64,a->previous.bytes);
    size_t at=header;
    cb_copy(data+at,a->base,base);
    at+=base;
    for(struct data_plan *p=a->plans;p;p=p->next){size_t n=cb_length(p->home);
        cb_reg_put(data+at,p->uid);
        cb_reg_put(data+at+8,p->gid);
        cb_reg_put(data+at+16,p->existed);
        cb_reg_put(data+at+24,n);
        cb_copy(data+at+32,p->home,n);
        at+=32+n;
        }
    cb_copy(data+at,a->next,a->next_bytes);
    at+=a->next_bytes;
    cb_copy(data+at,a->bundle.data,a->bundle.bytes);
    at+=a->bundle.bytes;
    cb_copy(data+at,a->previous.data,a->previous.bytes);at+=a->previous.bytes;
    crypto_sha512(data+at,data,at);
    (void)count;
    *out=(struct blob){data,bytes};
    return CB_OK;
    }
static int registry_blob(const unsigned char *data,size_t bytes){return bytes>=32 && cb_registry_magic(cb_reg_u64(data)) && cb_reg_u64(data+8)==bytes && cb_reg_u64(data+16);
    }
static cb_status_t decode_intent(struct blob *file,struct action *a,size_t *body,int *committed){
    if(file->bytes<64)return CB_NOT_FOUND;
    const unsigned char *p=file->data;
    unsigned header=(cb_install_equal(p,"CBINS002",8) || cb_install_equal(p,"CBINS003",8))?72:64;
    if(header==64 && !cb_install_equal(p,"CBINS001",8))return CB_RESOURCE_FAILURE;
    if(file->bytes<header)return CB_NOT_FOUND;
    uint64_t previous=header==72?cb_reg_u64(p+64):0;
    uint64_t op=cb_reg_u64(p+8),base=cb_reg_u64(p+32),plans=cb_reg_u64(p+40),reg=cb_reg_u64(p+48),bundle=cb_reg_u64(p+56);
    if((op<1 || op>6) || !base || base>4095 || reg<32 || (op!=5 && bundle<176) || !cb_frame_fits(128,base,plans,SIZE_MAX) || reg>SIZE_MAX-128-base-plans || bundle>SIZE_MAX-128-base-plans-reg)return CB_RESOURCE_FAILURE;
    size_t bytes=128+base+plans+reg+bundle;
    if(header-64>SIZE_MAX-bytes)return CB_BAD_REQUEST;
    if((op==3 && !cb_install_equal(p,"CBINS002",8)) || (op==5 && !cb_install_equal(p,"CBINS003",8)) || ((op==3 || op==5)!=(header==72)) || (op==3 && previous<176) || (op==5 && (previous<32 || bundle)) || previous>SIZE_MAX-bytes-(header-64))return CB_BAD_REQUEST;
    bytes+=previous+header-64;
    if(bytes>SIZE_MAX-8)return CB_BAD_REQUEST;
    if(file->bytes<bytes)return CB_NOT_FOUND;
    if(file->bytes>bytes+8)return CB_RESOURCE_FAILURE;
    unsigned char hash[64];
    crypto_sha512(hash,p,bytes-64);
    if(!cb_install_equal(hash,p+bytes-64,64))return CB_RESOURCE_FAILURE;
    *a=(struct action){.operation=op,.owner=cb_reg_u64(p+16),.options=cb_reg_u64(p+24)};
    if(!a->owner || (a->options & ~(op==1?CB_INSTALL_REUSE_DATA:(op==2 || op==5)?CB_INSTALL_DELETE_DATA:op==3?CB_INSTALL_ALLOW_DOWNGRADE:0)))return CB_BAD_REQUEST;
    cb_copy(a->base,p+header,base);
    a->base[base]=0;
    if(cb_length(a->base)!=base || !bundle_path((unsigned char *)a->base+1,base-1))return CB_BAD_REQUEST;
    size_t at=header+base,end=at+plans;
    while(at<end){if(end-at<32)return CB_BAD_REQUEST;
        uint64_t n=cb_reg_u64(p+at+24),uid=cb_reg_u64(p+at),gid=cb_reg_u64(p+at+8),existed=cb_reg_u64(p+at+16);
        if(!n || n>4095 || n>end-at-32 || existed>1)return CB_BAD_REQUEST;
        const unsigned char *h=p+at+32;
        if((!uid && (n!=12 || !cb_install_equal(h,"/system/root",12))) || (uid && (n<7 || !cb_install_equal(h,"/home/",6) || !id_name(h+6,n-6))))return CB_BAD_REQUEST;
        struct data_plan *plan=allocate(sizeof(*plan)+n+1);
        if(!plan)return CB_RESOURCE_FAILURE;
        *plan=(struct data_plan){a->plans,uid,gid,existed};
        cb_copy(plan->home,h,n);
        plan->home[n]=0;
        a->plans=plan;
        at+=32+n;
        }
    a->next_bytes=reg;
    a->next=allocate(reg);
    if(!a->next)return CB_RESOURCE_FAILURE;
    cb_copy(a->next,p+at,reg);
    at+=reg;
    if(!registry_blob(a->next,reg))return CB_BAD_REQUEST;
    uint64_t current_generation=registrations?cb_reg_u64(registrations+16):0,next_generation=cb_reg_u64(a->next+16);
    if(registrations && (next_generation<current_generation || next_generation>current_generation+1))return CB_DENIED;
    if(op==5){a->previous=(struct blob){allocate(previous),previous};if(!a->previous.data)return CB_RESOURCE_FAILURE;cb_copy(a->previous.data,p+at,previous);
        if(!registry_blob(a->previous.data,previous) || cb_reg_u64(a->previous.data+16)==UINT64_MAX || cb_reg_u64(a->previous.data+16)+1!=next_generation)return CB_DENIED;
        size_t removed_at=SIZE_MAX,removed_bytes=0;int found=0;for(size_t old=32;old<previous;){if(previous-old<80)return CB_BAD_REQUEST;struct cb_install_record r;cb_copy(&r,a->previous.data+old+16,64);if(r.bytes<64 || r.bytes>previous-old-16 || !cb_frame_fits(64,r.name_bytes,r.path_bytes,r.bytes) || r.manifest_bytes!=r.bytes-64-r.name_bytes-r.path_bytes)return CB_BAD_REQUEST;
            if(cb_reg_u64(a->previous.data+old)==a->owner && r.path_bytes==base && cb_install_equal(a->previous.data+old+80+r.name_bytes,a->base,base)){const char *leaf=basename(a->base);if(cb_length(leaf)!=34+r.name_bytes || leaf[0]!='_' || leaf[33]!='.' || !cb_install_equal(leaf+34,a->previous.data+old+80,r.name_bytes))return CB_DENIED;static const char hex[]="0123456789abcdef";for(unsigned j=0;j<16;++j)if(leaf[1+j*2]!=hex[r.installation[j]>>4] || leaf[2+j*2]!=hex[r.installation[j]&15])return CB_DENIED;cb_copy(a->identity,r.installation,16);removed_at=old;removed_bytes=16+r.bytes;found=1;}old+=16+r.bytes;}
        if(!found || reg!=previous-removed_bytes || cb_reg_u64(a->next+24)+1!=cb_reg_u64(a->previous.data+24))return CB_DENIED;
        size_t dest=32;for(size_t old=32;old<previous;){struct cb_install_record r;cb_copy(&r,a->previous.data+old+16,64);size_t n=16+r.bytes;if(old!=removed_at){if(n>reg-dest || !cb_install_equal(a->next+dest,a->previous.data+old,8) || !cb_install_equal(a->next+dest+16,a->previous.data+old+16,r.bytes))return CB_DENIED;uint64_t rev=cb_reg_u64(a->previous.data)==CB_REGISTRY_MAGIC?1:cb_reg_u64(a->previous.data+old+8);if((cb_reg_u64(a->next)==CB_REGISTRY_MAGIC?1:cb_reg_u64(a->next+dest+8))!=rev)return CB_DENIED;dest+=n;}old+=n;}if(dest!=reg)return CB_DENIED;
        {uint64_t token;cb_status_t valid=stage_registry(a->previous.data,previous,0,1,&token);if(valid)return valid;valid=registry(CB_REGISTRY_ABORT,0,token,0,0,0,NULL,0);if(valid)return valid;}
        if(a->owner==CB_REGISTRY_SYSTEM){if(!under(a->base,"/system/apps") || basename(a->base)!=a->base+13)return CB_DENIED;}else{const char *leaf=basename(a->base);if(!under(a->base,"/home") || leaf-a->base<7 || !cb_install_equal(leaf-7,"/_apps/",7))return CB_DENIED;}
        goto validated_target;
    }
    struct blob package={(unsigned char *)p+at,bundle};
    cb_status_t s=verify(&package,&a->bundle);
    if(s)return s;
    if(previous){a->previous.data=allocate(previous);if(!a->previous.data)return CB_RESOURCE_FAILURE;a->previous.bytes=previous;cb_copy(a->previous.data,p+at+bundle,previous);
        struct bundle prior;struct blob copy=a->previous;s=verify(&copy,&prior);if(s)return s;
        if(prior.name!=a->bundle.name || !cb_install_equal(prior.data+64,a->bundle.data+64,16) || !cb_install_equal(prior.data+prior.header,a->bundle.data+a->bundle.header,prior.name))return CB_DENIED;}
    const unsigned char *records=op!=2?a->next:registrations;
    size_t rb=op!=2?reg:registration_bytes;
    int found=0;
    for(at=32;at<rb;){if(rb-at<80)return CB_BAD_REQUEST;
        struct cb_install_record r;
        cb_copy(&r,records+at+16,64);
        if(r.bytes<64 || r.bytes>rb-at-16 || !cb_frame_fits(64,r.name_bytes,r.path_bytes,r.bytes) || r.manifest_bytes!=r.bytes-64-r.name_bytes-r.path_bytes)return CB_BAD_REQUEST;
        if(cb_reg_u64(records+at)==a->owner && cb_install_equal(r.installation,a->bundle.data+64,16)){if(r.path_bytes!=base || r.name_bytes!=a->bundle.name || !cb_install_equal(records+at+80+r.name_bytes,a->base,base))return CB_DENIED;
            if(op!=2 && ((cb_reg_u64(records)==CB_REGISTRY_MAGIC?1:cb_reg_u64(records+at+8))!=a->bundle.revision || !policy_matches(records+at+80+r.name_bytes+r.path_bytes,r.manifest_bytes,a->bundle.data+a->bundle.header+a->bundle.name,a->bundle.manifest)))return CB_DENIED;
            found=1;
            break;
            }at+=16+r.bytes;
        }if(!found && op!=2)return CB_DENIED;
    const char *leaf=basename(a->base);size_t bn=cb_length(leaf);if(bn!=34+a->bundle.name || leaf[0]!='_' || leaf[33]!='.' || !cb_install_equal(leaf+34,a->bundle.data+a->bundle.header,a->bundle.name))return CB_DENIED;
    static const char hex[]="0123456789abcdef";for(unsigned i=0;i<16;++i)if(leaf[1+i*2]!=hex[a->bundle.data[64+i]>>4] || leaf[2+i*2]!=hex[a->bundle.data[64+i]&15])return CB_DENIED;
    if(a->owner==CB_REGISTRY_SYSTEM){if(!under(a->base,"/system/apps") || leaf!=a->base+13)return CB_DENIED;}else{size_t prefix=leaf-a->base;if(!under(a->base,"/home") || prefix<7 || !cb_install_equal(leaf-7,"/_apps/",7))return CB_DENIED;}

validated_target:
    *body=bytes;
    *committed=file->bytes==bytes+8 && cb_install_equal(p+bytes,"COMMIT01",8);
    if(!*committed && (!registrations || next_generation<=current_generation))return CB_DENIED;
    if(*committed && registrations && next_generation==current_generation && (reg!=registration_bytes || !cb_install_equal(a->next,registrations,reg)))return CB_DENIED;
    return CB_OK;
    }
static cb_status_t commit_action(struct action *a,uint64_t token){
#ifdef CB_INSTALL_CRASH_POINT
    test_active_operation=a->operation;
#endif
    struct blob journal={0};
    cb_status_t s=intent(a,&journal);
    if(s)goto abort;
    phase=5;s=write_file(JOURNAL_PATH,journal.data,journal.bytes,CB_FILE_RECREATE);
    release(journal.data);
    if(s){failed=2;
        goto abort;
        }
    CB_INSTALL_FAULT(1);
    char package[160];
    package_path(a,package);
    if(a->operation==1 || a->operation==3 || a->operation==4){phase=6;s=(a->operation==3 || a->operation==4)?replace_artifacts(a):write_artifacts(a);
        if(!s && (a->operation==1 || a->operation==4)){phase=64;s=write_file(package,a->bundle.data,a->bundle.bytes,CB_FILE_RECREATE);}
        if(s){uint64_t failed_phase=phase;cb_status_t cleanup=a->operation==3?restore_previous(a):cleanup_artifacts(a,1);
            if(!cleanup && (a->operation==1 || a->operation==4))cleanup=remove_tree(package);
            if(!cleanup)cleanup=write_file(JOURNAL_PATH,NULL,0,CB_FILE_RECREATE);
            if(cleanup)failed=2;
            phase=failed_phase;goto abort;
            }}
    CB_INSTALL_FAULT(2);
    phase=7;s=write_file(JOURNAL_PATH,"COMMIT01",8,CB_FILE_APPEND);
    if(s){failed=2;
        return s;
        }
    CB_INSTALL_FAULT(3);
    phase=8;s=save_registry(a->next,a->next_bytes);
    if(s){failed=2;
        return s;
        }
    CB_INSTALL_FAULT(4);
    phase=9;s=registry(CB_REGISTRY_COMMIT,0,token,0,0,0,NULL,0);
    if(s){failed=2;
        return s;
        }
    release(registrations);
    registrations=a->next;
    registration_bytes=a->next_bytes;
    a->next=NULL;
    if(a->operation==4 || a->operation==5)rehabilitate(a->owner,a->identity);
    CB_INSTALL_FAULT(5);
    if(a->operation==3){phase=64;s=write_file(package,a->bundle.data,a->bundle.bytes,CB_FILE_RECREATE);}
    if(a->operation==2 || a->operation==5){phase=10;s=cleanup_artifacts(a,0);
        if(!s)s=remove_tree(package);
        }
    if(!s){phase=11;s=refresh_indexes(a);}
    if(!s)s=write_file(JOURNAL_PATH,NULL,0,CB_FILE_RECREATE);
    if(s)failed=2;
    return s;
    abort:(void)registry(CB_REGISTRY_ABORT,0,token,0,0,0,NULL,0);
    return s;
    }
static cb_status_t load_store(void){
    unsigned char *live=NULL;
    struct blob file={0};
    cb_status_t s=read_file(REGISTRY_PATH,&file);
    if(s && s!=CB_NOT_FOUND)return s;
    unsigned char hash[64];
    int valid=!s && file.bytes>=96 && registry_blob(file.data,file.bytes-64);
    if(valid){crypto_sha512(hash,file.data,file.bytes-64);
        valid=cb_install_equal(hash,file.data+file.bytes-64,64);
        }
    if(valid){registrations=file.data;registration_bytes=file.bytes-64;}
    else {release(file.data);registrations=NULL;registration_bytes=0;}
    struct blob journal={0};
    s=read_file(JOURNAL_PATH,&journal);
    if(s)return s;
    struct action recovery={0};
    int committed=0;
    size_t body=0;
    if(journal.bytes){s=decode_intent(&journal,&recovery,&body,&committed);
        if(s==CB_NOT_FOUND){if(!registrations){s=CB_DENIED;goto done;}
            s=write_file(JOURNAL_PATH,NULL,0,CB_FILE_RECREATE);
            }else if(!s){char package[160];
            package_path(&recovery,package);
            if(committed && recovery.operation==3){s=replace_artifacts(&recovery);if(!s)s=write_file(package,recovery.bundle.data,recovery.bundle.bytes,CB_FILE_RECREATE);if(s)goto done;}
            if(committed){s=save_registry(recovery.next,recovery.next_bytes);
                if(!s){release(registrations);
                    registrations=recovery.next;
                    registration_bytes=recovery.next_bytes;
                    recovery.next=NULL;
                    }if(!s && (recovery.operation==2 || recovery.operation==5)){s=cleanup_artifacts(&recovery,0);
                    if(!s)s=remove_tree(package);
                    }}else if(recovery.operation==1 || recovery.operation==4){s=cleanup_artifacts(&recovery,1);
                if(!s)s=remove_tree(package);
                }else if(recovery.operation==3)s=restore_previous(&recovery);
                }if(s)goto done;
        }
    /* A committed intent repairs an interrupted registry overwrite. Without
     * that authenticated intent, a missing/damaged registry fails closed. */
    if(!registrations){s=CB_DENIED;goto done;}
    /* Validate the complete registry structure/authority before interpreting it.
     * No dynamic app is published until its artifacts pass verification. */
    {uint64_t token;s=stage_registry(registrations,registration_bytes,0,1,&token);
        if(s)goto done;
        s=registry(CB_REGISTRY_ABORT,0,token,0,0,0,NULL,0);if(s)goto done;registry_trusted=1;}
    live=allocate(registration_bytes);if(!live){s=CB_RESOURCE_FAILURE;goto done;}
    cb_copy(live,registrations,32);size_t live_bytes=32;uint64_t live_count=0;
    /* Validate signed saved packages and installed bytes before any publication. */
    for(size_t at=32;at<registration_bytes;){if(registration_bytes-at<80){s=CB_BAD_REQUEST;
            goto done;
            }struct cb_install_record r;
        cb_copy(&r,registrations+at+16,64);
        if(r.bytes<64 || r.bytes>registration_bytes-at-16 || r.path_bytes>4095 || !cb_frame_fits(64,r.name_bytes,r.path_bytes,r.bytes)){s=CB_BAD_REQUEST;
            goto done;
            }struct action a={.owner=cb_reg_u64(registrations+at)};
        cb_copy(a.base,registrations+at+80+r.name_bytes,r.path_bytes);
        a.base[r.path_bytes]=0;
        unsigned char identity_bytes[80]={0};
        cb_copy(identity_bytes+64,r.installation,16);
        a.bundle.data=identity_bytes;
        char package[160];
        package_path(&a,package);
        struct blob saved={0};
        s=read_file(package,&saved);
        if(!s)s=verify(&saved,&a.bundle);
        if(!s && (a.bundle.revision!=(cb_reg_u64(registrations)==CB_REGISTRY_MAGIC?1:cb_reg_u64(registrations+at+8)) || r.name_bytes!=a.bundle.name || !policy_matches(registrations+at+80+r.name_bytes+r.path_bytes,r.manifest_bytes,a.bundle.data+a.bundle.header+a.bundle.name,a.bundle.manifest) || r.flags!=a.bundle.flags || r.stack_bytes!=a.bundle.stack || !cb_install_equal(r.installation,a.bundle.data+64,16) || !cb_install_equal(registrations+at+80,a.bundle.data+a.bundle.header,r.name_bytes)))s=CB_DENIED;
        if(!s)s=verify_artifacts(&a);
        release(saved.data);
        if(s){cb_status_t diagnosed=quarantine(a.owner,&r,registrations+at+80,s);if(diagnosed){s=diagnosed;goto done;}}
        else {cb_copy(live+live_bytes,registrations+at,16+r.bytes);live_bytes+=16+r.bytes;++live_count;}
        at+=16+r.bytes;
        }
    {uint64_t token;
        cb_reg_put(live+8,live_bytes);cb_reg_put(live+24,live_count);
        s=stage_registry(live,live_bytes,0,1,&token);
        if(s)goto done;
        /* Complete all fallible recovery I/O while publication is still gated.
         * COMMIT is the final kernel-only step; no disk work follows it. */
        if(journal.bytes && committed)s=refresh_indexes_from(&recovery,live,live_bytes);
        if(!s && journal.bytes)s=write_file(JOURNAL_PATH,NULL,0,CB_FILE_RECREATE);
        if(!s)s=registry(CB_REGISTRY_COMMIT,0,token,0,0,0,NULL,0);
        if(s){cb_status_t aborted=registry(CB_REGISTRY_ABORT,0,token,0,0,0,NULL,0);if(aborted)s=aborted;}
        }
    done:release(live);free_plans(&recovery);
    release(recovery.next);release(recovery.previous.data);
    release(journal.data);
    (void)body;
    return s;
    }

/* Essential built-ins never depend on optional package health. An untrusted
 * registry remains untouched on disk and permits only a built-in recovery shell. */
/* Success means a validated live registry or a verified empty containment
 * snapshot. The original storage failure remains available in STATUS. */
static cb_status_t initialize_store(void){
    cb_status_t status=load_store();
    if(!status)return CB_OK;
    failed=2;registry_trusted=0;last_status=status;phase=12;
    cb_status_t contained=registry(CB_REGISTRY_VERSION,0,0,0,0,0,NULL,0);
    if(contained)return contained;
    uint64_t generation;cb_copy(&generation,cb_result(&client),8);
    if(!generation)return CB_RESOURCE_FAILURE;
    release(registrations);registrations=allocate(32);registration_bytes=32;
    if(!registrations)return CB_RESOURCE_FAILURE;
    cb_reg_put(registrations,CB_REGISTRY_MAGIC_V2);cb_reg_put(registrations+8,32);
    cb_reg_put(registrations+16,generation);cb_reg_put(registrations+24,0);
    uint64_t token;
    contained=stage_registry(registrations,32,0,1,&token);
    if(contained)return contained;
    contained=registry(CB_REGISTRY_COMMIT,0,token,0,0,0,NULL,0);
    if(contained)(void)registry(CB_REGISTRY_ABORT,0,token,0,0,0,NULL,0);
    return contained;
}
