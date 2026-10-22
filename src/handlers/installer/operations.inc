static size_t installed(uint64_t owner,const unsigned char *name,size_t bytes,struct cb_install_record *record){for(size_t at=32;at<registration_bytes;){struct cb_install_record r;
        cb_copy(&r,registrations+at+16,64);
        if(cb_reg_u64(registrations+at)==owner && r.name_bytes==bytes && cb_install_equal(registrations+at+80,name,bytes)){*record=r;
            return at;
            }at+=16+r.bytes;
        }return SIZE_MAX;
    }
static cb_status_t new_registry(struct action *a,size_t old,const struct cb_install_record *record){
    struct bundle *b=&a->bundle;
    size_t path=cb_length(a->base),manifest=a->approved.data?a->approved.bytes:b->manifest,entry=80+b->name+path+manifest,removed=old==SIZE_MAX?0:16+record->bytes;
    if(a->operation==2 || a->operation==5)entry=0;
    if(registration_bytes-removed>SIZE_MAX-entry || cb_reg_u64(registrations+16)==UINT64_MAX)return CB_RESOURCE_FAILURE;
    a->next_bytes=registration_bytes-removed+entry;
    a->next=allocate(a->next_bytes);
    if(!a->next)return CB_RESOURCE_FAILURE;
    size_t before=old==SIZE_MAX?registration_bytes:old;
    cb_copy(a->next,registrations,before);
    if(removed)cb_copy(a->next+before+entry,registrations+old+removed,registration_bytes-old-removed);
    if(entry){unsigned char *p=a->next+before;
        cb_reg_put(p,a->owner);
        cb_reg_put(p+8,b->revision);
        cb_reg_put(p+16,entry-16);
        cb_reg_put(p+24,b->name);
        cb_reg_put(p+32,path);
        cb_reg_put(p+40,manifest);
        cb_copy(p+48,b->data+64,16);
        id_put32(p+64,b->flags);
        id_put32(p+68,0);
        cb_reg_put(p+72,b->stack);
        cb_copy(p+80,b->data+b->header,b->name);
        cb_copy(p+80+b->name,a->base,path);
        cb_copy(p+80+b->name+path,a->approved.data?a->approved.data:b->data+b->header+b->name,manifest);
        }
    if(cb_reg_u64(a->next)==CB_REGISTRY_MAGIC)for(size_t at=32;at<a->next_bytes;){struct cb_install_record r;cb_copy(&r,a->next+at+16,64);if(!cb_reg_u64(a->next+at+8))cb_reg_put(a->next+at+8,1);at+=16+r.bytes;}
    cb_reg_put(a->next,CB_REGISTRY_MAGIC_V2);
    cb_reg_put(a->next+8,a->next_bytes);
    cb_reg_put(a->next+16,cb_reg_u64(registrations+16)+1);
    cb_reg_put(a->next+24,cb_reg_u64(registrations+24)+(entry?1:0)-(removed?1:0));
    return CB_OK;
    }
static cb_status_t operate(uint64_t pid,unsigned operation,const unsigned char *payload,size_t bytes,size_t *result){
    if(operation==CB_INSTALLER_STATUS && !bytes){uint64_t state[4]={registrations?cb_reg_u64(registrations+16):0,failed,phase,last_status};
        cb_copy(output,state,sizeof(state));
        *result=sizeof(state);
        return CB_OK;
        }
    if(failed>=2 && operation!=CB_INSTALLER_LIST && operation!=CB_INSTALLER_DIAGNOSTICS)return CB_RESOURCE_FAILURE;
    struct cb_install_context who;
    char home[4096];
    cb_status_t s=context(pid,&who,home);
    if(s)return s;
    if(operation==CB_INSTALLER_DIAGNOSTICS && bytes==8){uint64_t cursor;cb_copy(&cursor,payload,8);uint64_t index=0;
        for(struct rejected_app *p=rejected;p;p=p->next)if(p->owner==who.uid || p->owner==CB_REGISTRY_SYSTEM){if(index++==cursor){struct cb_install_diagnostic d={cursor+1,p->owner,p->status,p->bytes,{0}};cb_copy(d.installation,p->identity,16);cb_copy(output,&d,sizeof(d));cb_copy(output+sizeof(d),p->name,p->bytes);*result=sizeof(d)+p->bytes;break;}}
        return CB_OK;
        }
    if(operation==CB_INSTALLER_LIST && bytes==8){uint64_t cursor;
        cb_copy(&cursor,payload,8);
        uint64_t index=0;
        for(size_t at=32;at<registration_bytes;){struct cb_install_record r;
            cb_copy(&r,registrations+at+16,64);
            uint64_t uid=cb_reg_u64(registrations+at);
            if(uid==who.uid || uid==CB_REGISTRY_SYSTEM){if(index++==cursor){struct cb_registry_entry e={cb_reg_u64(registrations+16),cursor+1,r.name_bytes,uid,{0},r.flags,unavailable(uid,r.installation)?2:1};
                    cb_copy(e.installation,r.installation,16);
                    cb_copy(output,&e,sizeof(e));
                    cb_copy(output+sizeof(e),registrations+at+80,r.name_bytes);
                    *result=sizeof(e)+r.name_bytes;
                    return CB_OK;
                    }}at+=16+r.bytes;
            }return CB_OK;
        }
    if(bytes<sizeof(struct cb_installer_request))return CB_BAD_REQUEST;
    struct cb_installer_request q;cb_copy(&q,payload,sizeof(q));
    if(!q.name_bytes || q.name_bytes>4095 || q.scope>CB_INSTALL_SCOPE_USER || q.name_bytes>bytes-sizeof(q))return CB_BAD_REQUEST;
    size_t tail=sizeof(q)+q.name_bytes;char name[4096],policy_path[4096];cb_copy(name,payload+sizeof(q),q.name_bytes);name[q.name_bytes]=0;
    if(cb_length(name)!=q.name_bytes)return CB_BAD_REQUEST;
    if(q.options&CB_INSTALL_APPROVAL_FILE){if(bytes-tail<8)return CB_BAD_REQUEST;uint64_t n=cb_reg_u64(payload+tail);tail+=8;if(!n || n>4095 || n!=bytes-tail)return CB_BAD_REQUEST;cb_copy(policy_path,payload+tail,n);policy_path[n]=0;if(cb_length(policy_path)!=n || policy_path[0]!='/' || !bundle_path((unsigned char *)policy_path+1,n-1))return CB_BAD_REQUEST;tail+=n;}
    if(tail!=bytes)return CB_BAD_REQUEST;
    int inspect=operation==CB_INSTALLER_INSPECT || operation==CB_INSTALLER_PERMISSIONS;
    unsigned allowed=CB_INSTALL_APPROVAL_FILE|(inspect?CB_INSTALL_SHOW_REQUESTED:0)|(operation==CB_INSTALLER_UPGRADE?CB_INSTALL_ALLOW_DOWNGRADE:0)|(operation==CB_INSTALLER_INSTALL?CB_INSTALL_REUSE_DATA:0)|(operation==CB_INSTALLER_REMOVE?CB_INSTALL_DELETE_DATA:0);
    if(q.options&~allowed || (!inspect && q.offset))return CB_BAD_REQUEST;
    if(operation!=CB_INSTALLER_INSPECT && operation!=CB_INSTALLER_INSTALL && operation!=CB_INSTALLER_REMOVE && operation!=CB_INSTALLER_UPGRADE && operation!=CB_INSTALLER_REPAIR && operation!=CB_INSTALLER_PERMISSIONS && operation!=CB_INSTALLER_APPROVE && operation!=CB_INSTALLER_METADATA)return CB_BAD_REQUEST;
    uint64_t owner=(q.scope==CB_INSTALL_SCOPE_SYSTEM || (!q.scope && !who.uid))?CB_REGISTRY_SYSTEM:who.uid;
    if((owner==CB_REGISTRY_SYSTEM && !who.administrator) || owner==0)return CB_DENIED;
    struct blob package={0},explicit={0};struct action a={.operation=operation==CB_INSTALLER_REMOVE?2:operation==CB_INSTALLER_UPGRADE?3:operation==CB_INSTALLER_REPAIR?4:operation==CB_INSTALLER_APPROVE?6:1,.owner=owner,.options=q.options};
    struct cb_install_record old_record={0};size_t old=SIZE_MAX;
    int named=operation==CB_INSTALLER_REMOVE || operation==CB_INSTALLER_PERMISSIONS || operation==CB_INSTALLER_APPROVE;
    if(named){old=installed(owner,(unsigned char *)name,q.name_bytes,&old_record);if(old==SIZE_MAX)return CB_NOT_FOUND;cb_copy(a.identity,old_record.installation,16);cb_copy(a.base,registrations+old+80+old_record.name_bytes,old_record.path_bytes);a.base[old_record.path_bytes]=0;
        if(operation==CB_INSTALLER_REMOVE && unavailable(owner,a.identity)){a.operation=5;a.previous=(struct blob){allocate(registration_bytes),registration_bytes};if(!a.previous.data){s=CB_RESOURCE_FAILURE;goto done;}cb_copy(a.previous.data,registrations,registration_bytes);goto mutation;}
        char path[160];package_path(&a,path);s=read_file(path,&package);
    }else{if(name[0]!='/' || !bundle_path((unsigned char *)name+1,q.name_bytes-1))return CB_BAD_REQUEST;if(who.uid && !under(name,home) && !under(name,"/system/packages"))return CB_DENIED;s=registry(CB_REGISTRY_SOURCE,pid,0,0,q.name_bytes,0,name,q.name_bytes);if(!s)s=read_file(name,&package);}
    if(s)goto done;
    phase=1;s=verify(&package,&a.bundle);if(s)goto done;cb_copy(a.identity,a.bundle.data+64,16);
    if(old==SIZE_MAX)old=installed(owner,a.bundle.data+a.bundle.header,a.bundle.name,&old_record);
    uint64_t prior_revision=old==SIZE_MAX?0:cb_reg_u64(registrations)==CB_REGISTRY_MAGIC?1:cb_reg_u64(registrations+old+8);
    if(operation==CB_INSTALLER_METADATA){struct cb_release_info info={a.bundle.revision,prior_revision,owner,{0},{0}};cb_copy(info.issuer,a.bundle.data+80,32);cb_copy(info.fingerprint,a.bundle.fingerprint,64);cb_copy(output,&info,sizeof(info));*result=sizeof(info);goto done;}
    if(q.options&CB_INSTALL_APPROVAL_FILE){size_t n=cb_length(policy_path);s=registry(CB_REGISTRY_SOURCE,pid,0,0,n,0,policy_path,n);if(!s)s=read_file(policy_path,&explicit);if(s)goto done;if(!explicit.bytes){s=CB_BAD_REQUEST;goto done;}}
    const void *prior=old==SIZE_MAX?NULL:registrations+old+80+old_record.name_bytes+old_record.path_bytes;
    s=policy_select(&a.bundle,prior,old_record.manifest_bytes,&explicit,&a.approved);if(s)goto done;
    unsigned char review_hash[64];size_t review_bytes=a.approved.bytes+64;if(review_bytes<a.approved.bytes){s=CB_RESOURCE_FAILURE;goto done;}unsigned char *review=allocate(review_bytes);if(!review){s=CB_RESOURCE_FAILURE;goto done;}cb_copy(review,a.bundle.fingerprint,64);cb_copy(review+64,a.approved.data,a.approved.bytes);crypto_sha512(review_hash,review,review_bytes);release(review);
    if(inspect){const unsigned char *json=q.options&CB_INSTALL_SHOW_REQUESTED?a.bundle.data+a.bundle.header+a.bundle.name:a.approved.data;size_t json_bytes=q.options&CB_INSTALL_SHOW_REQUESTED?a.bundle.manifest:a.approved.bytes;
        if(q.offset>json_bytes){s=CB_BAD_REQUEST;goto done;}struct cb_bundle_info info={a.bundle.name,json_bytes,a.bundle.code,a.bundle.resources,a.bundle.flags,a.bundle.stack,0,{0},{0}};cb_copy(info.installation,a.identity,16);cb_copy(info.fingerprint,review_hash,64);size_t n=json_bytes-q.offset,room=sizeof(output)-1024-sizeof(info)-a.bundle.name;if(n>room)n=room;info.next=q.offset+n;cb_copy(output,&info,sizeof(info));cb_copy(output+sizeof(info),a.bundle.data+a.bundle.header,a.bundle.name);cb_copy(output+sizeof(info)+a.bundle.name,json+q.offset,n);*result=sizeof(info)+a.bundle.name+n;goto done;}
    if(operation!=CB_INSTALLER_REMOVE && !cb_install_equal(q.expected_hash,review_hash,64)){s=CB_DENIED;goto done;}
    if(a.operation==6 && !(q.options&CB_INSTALL_APPROVAL_FILE)){s=CB_BAD_REQUEST;goto done;}
    if(a.operation==1 && old!=SIZE_MAX){s=CB_BUSY;goto done;}
    if(a.operation==3 || a.operation==4 || a.operation==6){
        if(old==SIZE_MAX){s=CB_NOT_FOUND;goto done;}if(!cb_install_equal(old_record.installation,a.identity,16)){s=CB_DENIED;goto done;}
        cb_copy(a.base,registrations+old+80+old_record.name_bytes,old_record.path_bytes);a.base[old_record.path_bytes]=0;
        if(a.operation==4){if(!registry_trusted || !unavailable(owner,a.identity) || a.bundle.revision<prior_revision){s=CB_DENIED;goto done;}}
        if(a.operation==3){if(a.bundle.revision==prior_revision || (a.bundle.revision<prior_revision && !(q.options&CB_INSTALL_ALLOW_DOWNGRADE))){s=CB_DENIED;goto done;}char path[160];package_path(&a,path);s=read_file(path,&a.previous);if(s)goto done;struct bundle previous;s=verify(&a.previous,&previous);if(s)goto done;if(!cb_install_equal(previous.data+80,a.bundle.data+80,32)){s=CB_DENIED;goto done;}struct action existing=a;existing.bundle=previous;s=verify_artifacts(&existing);if(s)goto done;}
    }
    if(a.operation==1){size_t at=0;if(owner==CB_REGISTRY_SYSTEM){cb_copy(a.base,"/system/apps/",13);at=13;}else{struct cb_filesystem_metadata m;s=metadata(home,&m);if(s)goto done;if(!m.directory || m.uid!=who.uid){s=CB_DENIED;goto done;}size_t n=cb_length(home);if(n>4095-7-34-a.bundle.name-15){s=CB_BAD_REQUEST;goto done;}cb_copy(a.base,home,n);cb_copy(a.base+n,"/_apps/",7);at=n+7;}static const char hex[]="0123456789abcdef";a.base[at++]='_';for(unsigned i=0;i<16;++i){a.base[at++]=hex[a.identity[i]>>4];a.base[at++]=hex[a.identity[i]&15];}a.base[at++]='.';cb_copy(a.base+at,a.bundle.data+a.bundle.header,a.bundle.name);a.base[at+a.bundle.name]=0;
        const char *tails[]={"",".manifest.json",".resources"};for(unsigned i=0;i<3;++i){char path[4096];s=suffix(path,a.base,tails[i]);if(s)goto done;struct cb_filesystem_metadata m;s=metadata(path,&m);if(!s){s=CB_BUSY;goto done;}if(s!=CB_NOT_FOUND)goto done;s=CB_OK;}}
mutation:
    if(failed && !(failed==1 && registry_trusted && (a.operation==4 || a.operation==5))){s=CB_RESOURCE_FAILURE;goto done;}
    if(a.operation!=6){phase=2;s=data_plans(&a,&who,home);if(s)goto done;}
    phase=3;s=new_registry(&a,old,&old_record);if(s)goto done;
    {uint64_t token;phase=4;struct cb_registry_target guard={owner,{0}};cb_copy(guard.installation,a.identity,16);s=stage_publication(&a,pid,&token,(a.operation==3 || a.operation==6)?&guard:NULL);if(!s)s=commit_action(&a,token);}
done:free_plans(&a);release(a.next);release(package.data);release(a.previous.data);release(a.approved.data);release(explicit.data);return s;
}
