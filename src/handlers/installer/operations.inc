static size_t installed(uint64_t owner,const unsigned char *name,size_t bytes,struct cb_install_record *record){for(size_t at=32;at<registration_bytes;){struct cb_install_record r;
        cb_copy(&r,registrations+at+16,64);
        if(cb_reg_u64(registrations+at)==owner && r.name_bytes==bytes && cb_install_equal(registrations+at+80,name,bytes)){*record=r;
            return at;
            }at+=16+r.bytes;
        }return SIZE_MAX;
    }
static cb_status_t new_registry(struct action *a,size_t old,const struct cb_install_record *record){
    struct bundle *b=&a->bundle;
    size_t path=cb_length(a->base),entry=80+b->name+path+b->manifest,removed=old==SIZE_MAX?0:16+record->bytes;
    if(a->operation==2)entry=0;
    if(registration_bytes-removed>SIZE_MAX-entry || cb_reg_u64(registrations+16)==UINT64_MAX)return CB_RESOURCE_FAILURE;
    a->next_bytes=registration_bytes-removed+entry;
    a->next=allocate(a->next_bytes);
    if(!a->next)return CB_RESOURCE_FAILURE;
    size_t before=old==SIZE_MAX?registration_bytes:old;
    cb_copy(a->next,registrations,before);
    if(removed)cb_copy(a->next+before,registrations+old+removed,registration_bytes-old-removed);
    if(entry){unsigned char *p=a->next+before;
        cb_reg_put(p,a->owner);
        cb_reg_put(p+8,0);
        cb_reg_put(p+16,entry-16);
        cb_reg_put(p+24,b->name);
        cb_reg_put(p+32,path);
        cb_reg_put(p+40,b->manifest);
        cb_copy(p+48,b->data+64,16);
        id_put32(p+64,b->flags);
        id_put32(p+68,0);
        cb_reg_put(p+72,b->stack);
        cb_copy(p+80,b->data+112,b->name);
        cb_copy(p+80+b->name,a->base,path);
        cb_copy(p+80+b->name+path,b->data+112+b->name,b->manifest);
        }
    cb_reg_put(a->next+8,a->next_bytes);
    cb_reg_put(a->next+16,cb_reg_u64(registrations+16)+1);
    cb_reg_put(a->next+24,cb_reg_u64(registrations+24)+(entry?1:0)-(removed?1:0));
    return CB_OK;
    }
static cb_status_t operate(uint64_t pid,unsigned operation,const unsigned char *payload,size_t bytes,size_t *result){
    if(operation==CB_INSTALLER_STATUS && !bytes){uint64_t state[4]={cb_reg_u64(registrations+16),failed,phase,last_status};
        cb_copy(output,state,sizeof(state));
        *result=sizeof(state);
        return CB_OK;
        }
    if(failed)return CB_RESOURCE_FAILURE;
    struct cb_install_context who;
    char home[4096];
    cb_status_t s=context(pid,&who,home);
    if(s)return s;
    if(operation==CB_INSTALLER_LIST && bytes==8){uint64_t cursor;
        cb_copy(&cursor,payload,8);
        uint64_t index=0;
        for(size_t at=32;at<registration_bytes;){struct cb_install_record r;
            cb_copy(&r,registrations+at+16,64);
            uint64_t uid=cb_reg_u64(registrations+at);
            if(uid==who.uid || uid==CB_REGISTRY_SYSTEM){if(index++==cursor){struct cb_registry_entry e={cb_reg_u64(registrations+16),cursor+1,r.name_bytes,uid,{0},r.flags,1};
                    cb_copy(e.installation,r.installation,16);
                    cb_copy(output,&e,sizeof(e));
                    cb_copy(output+sizeof(e),registrations+at+80,r.name_bytes);
                    *result=sizeof(e)+r.name_bytes;
                    return CB_OK;
                    }}at+=16+r.bytes;
            }return CB_OK;
        }
    if(bytes<sizeof(struct cb_installer_request))return CB_BAD_REQUEST;
    struct cb_installer_request q;
    cb_copy(&q,payload,sizeof(q));
    if(!q.name_bytes || q.name_bytes>4095 || bytes!=sizeof(q)+q.name_bytes || q.scope>CB_INSTALL_SCOPE_USER)return CB_BAD_REQUEST;
    char name[4096];
    cb_copy(name,payload+sizeof(q),q.name_bytes);
    name[q.name_bytes]=0;
    if(cb_length(name)!=q.name_bytes)return CB_BAD_REQUEST;
    if(operation!=CB_INSTALLER_INSPECT && operation!=CB_INSTALLER_INSTALL && operation!=CB_INSTALLER_REMOVE)return CB_BAD_REQUEST;
    if(operation!=CB_INSTALLER_INSPECT && q.offset)return CB_BAD_REQUEST;
    if(q.options & ~(operation==CB_INSTALLER_REMOVE?CB_INSTALL_DELETE_DATA:operation==CB_INSTALLER_INSTALL?CB_INSTALL_REUSE_DATA:0))return CB_BAD_REQUEST;
    uint64_t owner=(q.scope==CB_INSTALL_SCOPE_SYSTEM || (!q.scope && !who.uid))?CB_REGISTRY_SYSTEM:who.uid;
    if(owner==CB_REGISTRY_SYSTEM && !who.administrator)return CB_DENIED;
    if(owner==0)return CB_DENIED;
    struct blob package={0};
    struct action a={.operation=operation==CB_INSTALLER_REMOVE?2:1,.owner=owner,.options=q.options};
    struct cb_install_record old_record={0};
    size_t old=SIZE_MAX;
    if(operation==CB_INSTALLER_REMOVE){old=installed(owner,(unsigned char *)name,q.name_bytes,&old_record);
        if(old==SIZE_MAX)return CB_NOT_FOUND;
        if(old_record.path_bytes>4095)return CB_BAD_REQUEST;
        cb_copy(a.base,registrations+old+80+old_record.name_bytes,old_record.path_bytes);
        a.base[old_record.path_bytes]=0;
        unsigned char raw[80]={0};
        cb_copy(raw+64,old_record.installation,16);
        a.bundle.data=raw;
        char path[160];
        package_path(&a,path);
        s=read_file(path,&package);
        }
    else {if(name[0]!='/' || !bundle_path((unsigned char *)name+1,q.name_bytes-1))return CB_BAD_REQUEST;
        if(who.uid && !under(name,home) && !under(name,"/system/packages"))return CB_DENIED;
        s=registry(CB_REGISTRY_SOURCE,pid,0,0,q.name_bytes,0,name,q.name_bytes);
        if(!s)s=read_file(name,&package);
        }
    if(s)goto done;
    phase=1;s=verify(&package,&a.bundle);
    if(s)goto done;
    if(operation==CB_INSTALLER_INSPECT){if(q.offset>a.bundle.manifest){s=CB_BAD_REQUEST;
            goto done;
            }struct cb_bundle_info info={a.bundle.name,a.bundle.manifest,a.bundle.code,a.bundle.resources,a.bundle.flags,a.bundle.stack,0,{0},{0}};
        cb_copy(info.installation,a.bundle.data+64,16);
        cb_copy(info.fingerprint,a.bundle.fingerprint,64);
        size_t n=a.bundle.manifest-q.offset,room=sizeof(output)-1024-sizeof(info)-a.bundle.name;
        if(n>room)n=room;
        info.next=q.offset+n;
        cb_copy(output,&info,sizeof(info));
        cb_copy(output+sizeof(info),a.bundle.data+112,a.bundle.name);
        cb_copy(output+sizeof(info)+a.bundle.name,a.bundle.data+112+a.bundle.name+q.offset,n);
        *result=sizeof(info)+a.bundle.name+n;
        goto done;
        }
    if(operation==CB_INSTALLER_INSTALL){if(!cb_install_equal(q.expected_hash,a.bundle.fingerprint,64)){s=CB_DENIED;
            goto done;
            }old=installed(owner,a.bundle.data+112,a.bundle.name,&old_record);
        if(old!=SIZE_MAX){s=CB_BUSY;
            goto done;
            }
        size_t at=0;
        if(owner==CB_REGISTRY_SYSTEM){cb_copy(a.base,"/system/apps/",13);
            at=13;
            }else{struct cb_filesystem_metadata m;
            s=metadata(home,&m);
            if(s)goto done;
            if(!m.directory || m.uid!=who.uid){s=CB_DENIED;
                goto done;
                }size_t n=cb_length(home);
            if(n>4095-7-34-a.bundle.name-15){s=CB_BAD_REQUEST;
                goto done;
                }cb_copy(a.base,home,n);
            cb_copy(a.base+n,"/_apps/",7);
            at=n+7;
            }
        static const char hex[]="0123456789abcdef";
        a.base[at++]='_';
        for(unsigned i=0;i<16;++i){a.base[at++]=hex[a.bundle.data[64+i]>>4];
            a.base[at++]=hex[a.bundle.data[64+i]&15];
            }a.base[at++]='.';
        cb_copy(a.base+at,a.bundle.data+112,a.bundle.name);
        a.base[at+a.bundle.name]=0;
        const char *tails[]={"",".manifest.json",".resources"};
        for(unsigned i=0;i<3;++i){char path[4096];
            s=suffix(path,a.base,tails[i]);
            if(s)goto done;
            struct cb_filesystem_metadata m;
            s=metadata(path,&m);
            if(!s){s=CB_BUSY;
                goto done;
                }if(s!=CB_NOT_FOUND)goto done;
            s=CB_OK;
            }
        size_t base=cb_length(a.base)+11,offset=a.bundle.resources_at;
        for(uint64_t i=0;i<a.bundle.resources;++i){size_t n=cb_reg_u64(a.bundle.data+offset),data=cb_reg_u64(a.bundle.data+offset+8);
            if(base+n>4095){s=CB_BAD_REQUEST;
                goto done;
                }offset+=16+n+data;
            }
    }
    phase=2;s=data_plans(&a,&who,home);
    if(s)goto done;
    phase=3;s=new_registry(&a,old,&old_record);
    if(s)goto done;
    {uint64_t token;
        phase=4;s=stage_registry(a.next,a.next_bytes,pid,0,&token);
        if(!s)s=commit_action(&a,token);
        }
done:free_plans(&a);
    release(a.next);
    release(package.data);
    return s;
    }
