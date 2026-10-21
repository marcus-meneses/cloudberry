/* Stack-owned wait registrations are linked and scanned only under CPU_ENTER.
 * IDs are re-resolved after every wake: no reclaimable object pointer survives
 * the unlock/block interval. Existing scheduler wake credits close that race. */
static struct request *owned_request(uint64_t pid,struct cb_request_id id){
    for(struct request *r=state.requests;r;r=r->next)if(!r->abandoned && r->pid==pid && r->id.namespace_id==id.namespace_id && r->id.serial==id.serial)return r;
    return NULL;
}
static void wait_request_changed(struct request *r){
    for(struct handle_waiter *w=handle_waiters;w;w=w->next)if(w->pid==r->pid)for(size_t i=0;i<w->count;++i){const struct cb_wait_item *item=w->items+i;if(item->kind==CB_WAIT_REQUEST && item->request.namespace_id==r->id.namespace_id && item->request.serial==r->id.serial){wake(w->thread);break;}}
}
static void wait_cap_changed(struct capability *cap){
    for(struct handle_waiter *w=handle_waiters;w;w=w->next)if(w->pid==cap->pid)for(size_t i=0;i<w->count;++i)if(w->items[i].kind!=CB_WAIT_REQUEST && w->items[i].handle==cap->id){wake(w->thread);break;}
}
static void wait_stream_changed(struct stream *stream){
    for(struct handle_waiter *w=handle_waiters;w;w=w->next)for(size_t i=0;i<w->count;++i)if(w->items[i].kind!=CB_WAIT_REQUEST){struct capability *cap=capability(w->pid,w->items[i].handle);if(cap && cap->stream==stream){wake(w->thread);break;}}
}
static cb_status_t wait_scan(uint64_t pid,const struct cb_wait_item *items,size_t count,uint64_t *ready){
    *ready=UINT64_MAX;struct policy *owner=policy(pid);if(!owner)return CB_REVOKED;if(owner->revoked && !count)return CB_REVOKED;
    for(size_t i=0;i<count;++i){struct cb_wait_item item;copy_bytes(&item,items+i,sizeof(item));int available;
        if(item.reserved)return CB_BAD_REQUEST;
        if(item.kind==CB_WAIT_REQUEST){
            if(item.handle || !item.request.serial)return CB_BAD_REQUEST;
            struct request *r=owned_request(pid,item.request);if(!r)return CB_BAD_HANDLE;
            available=r->done || r->failure;
        }else if(item.kind==CB_WAIT_READ || item.kind==CB_WAIT_WRITE){
            if(owner->revoked)return CB_REVOKED;
            if(item.request.serial || item.request.namespace_id)return CB_BAD_REQUEST;
            struct capability *cap=capability(pid,item.handle);if(!cap || !cap->stream)return CB_BAD_HANDLE;
            unsigned direction=item.kind==CB_WAIT_READ?CB_STREAM_READ:CB_STREAM_WRITE;
            if(cap->direction!=direction)return CB_DENIED;
            struct stream *s=cap->stream;
            available=s->filesystem?!s->busy:direction==CB_STREAM_READ?(s->used || !s->writers):(s->used<CB_PIPE_CAPACITY || !s->readers);
        }else return CB_BAD_REQUEST;
        if(available && *ready==UINT64_MAX)*ready=i;
    }
    return CB_OK;
}
static cb_status_t wait_any(struct policy *owner,const void *input,size_t bytes,uint64_t deadline,uint64_t *ready){
    if((!bytes && !deadline) || bytes%sizeof(struct cb_wait_item))return CB_BAD_REQUEST;
    struct handle_waiter waiter={.pid=owner->pid,.items=input,.count=bytes/sizeof(struct cb_wait_item)};
    uint64_t flags=lock();waiter.thread=CALL(state.control,THREAD_CURRENT,0,0).value;
    if(!waiter.thread){unlock(flags);return CB_BAD_REQUEST;}
    waiter.next=handle_waiters;handle_waiters=&waiter;cb_status_t status;
    for(;;){status=wait_scan(owner->pid,waiter.items,waiter.count,ready);if(status || *ready!=UINT64_MAX)break;
        /* Test under the registration lock; a wake before THREAD_BLOCK becomes
           a credit, not a missed notification. Recheck even after spurious wakes. */
        if(deadline && now()>=deadline){status=CB_TIMEOUT;break;}
        unlock(flags);cb_status_t slept=sleep_until(deadline);flags=lock();
        if(slept){status=wait_scan(owner->pid,waiter.items,waiter.count,ready);if(!status && *ready==UINT64_MAX)status=slept;break;}
    }
    struct handle_waiter **at=&handle_waiters;while(*at!=&waiter)at=&(*at)->next;*at=waiter.next;unlock(flags);return status;
}
static cb_status_t collect(struct policy *owner,struct cb_request_id id,unsigned char **out,size_t *bytes,size_t capacity){
    uint64_t flags=lock();struct request *r=owned_request(owner->pid,id);
    if(!r){unlock(flags);return CB_BAD_HANDLE;}
    if(!r->done && !r->failure){unlock(flags);return CB_WOULD_BLOCK;}
    if(r->failure){cb_status_t status=r->failure;unlock(flags);return status;}
    *bytes=r->reply_bytes;if(capacity<*bytes){unlock(flags);return CB_BUFFER_TOO_SMALL;}
    *out=allocate(*bytes);if(!*out){*bytes=0;unlock(flags);return CB_RESOURCE_FAILURE;}
    copy_bytes(*out,r->reply,*bytes);unlock(flags);return CB_OK;
}

/* Remove queued work atomically with provider dispatch. Dispatched calls retain
 * their owned payload/publication until a reply or provider death, then discard
 * the result without allocating a new reply. The caller cannot reuse this ID. */
static cb_status_t abandon(struct policy *owner,struct cb_request_id id,struct cb_abandon_result *result){
    uint64_t flags=lock();struct request *r=owned_request(owner->pid,id);if(!r){unlock(flags);return CB_BAD_HANDLE;}
    if(r->waiter){unlock(flags);return CB_BUSY;}
    result->phase=r->completed || (!r->dispatched && r->done)?CB_REQUEST_COMPLETE:r->dispatched?CB_REQUEST_DISPATCHED:CB_REQUEST_QUEUED;
    result->outcome=r->dispatched?CB_OUTCOME_UNKNOWN:CB_NOT_DISPATCHED;
    if(r->reply){struct cb_reply_header h;copy_bytes(&h,r->reply,sizeof(h));result->outcome=h.outcome;}
    wait_request_changed(r);
    if(!r->dispatched || r->completed)drop_request(r);
    else {r->abandoned=1;release(r->reply);r->reply=NULL;r->reply_bytes=0;r->done=0;r->failure=0;}
    unlock(flags);return CB_OK;
}
