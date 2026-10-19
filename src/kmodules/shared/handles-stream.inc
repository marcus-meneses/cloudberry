/* Kernel I/O buffering mechanism. Caller and result policy stay in handlers.
 * Waiter registrations live on blocked thread stacks, linked only under CPU_ENTER.
 * Wake credits close the unlock/block race; no polling and no ISR allocation. */
struct stream_waiter {struct stream_waiter *next;uint64_t thread;};
struct stream {uint64_t filesystem,offset;char *path;unsigned file_mode,busy;size_t head,used,readers,writers;struct stream_waiter *waiters;unsigned char data[];};
static void stream_wake(struct stream *s){for(struct stream_waiter *w=s->waiters;w;w=w->next)wake(w->thread);}
static void stream_drop(struct capability *cap){if(!cap->stream)return;struct stream *s=cap->stream;if(cap->direction==CB_STREAM_READ)--s->readers;else --s->writers;stream_wake(s);if(!s->readers && !s->writers){release(s->path);release(s);}}
static struct capability *stream_endpoint(struct policy *owner,struct publication *p,struct stream *stream,unsigned direction){
    struct operation op={.name=direction==CB_STREAM_READ?"read":"write"};struct grant g={.name=CB_STREAM_NAME,.scope="self",.operations=&op};
    struct capability *c=new_capability(owner,p,&g,0);if(!c)return NULL;c->stream=stream;c->direction=direction;if(direction==CB_STREAM_READ)++stream->readers;else ++stream->writers;return c;
}
struct file_job {uint64_t filesystem,offset;const char *path;unsigned op,mode;void *buffer;size_t bytes,actual;cb_status_t status;};
static unsigned file_error(uint64_t fs){struct ext4_info info={0};kernel_surface_call f=kernel_surface_find(state.hub,"ext4_fs.info");return f && !CALL(f,fs,&info,0).status?(info.error?info.error:EXT4_IO):EXT4_IO;}
static cb_status_t file_status(unsigned error){return error==EXT4_READ_ONLY?CB_READ_ONLY:error==EXT4_NOT_FOUND?CB_NOT_FOUND:error?CB_RESOURCE_FAILURE:CB_OK;}
struct kernel_surface_result handles_surface_file_worker(MODULE_ARGS){
    MODULE_UNUSED();struct file_job *j=(void *)a;kernel_surface_call stat=kernel_surface_find(state.hub,"ext4_fs.stat"),read=kernel_surface_find(state.hub,"ext4_fs.read"),write=kernel_surface_find(state.hub,"ext4_fs.write"),mutate=kernel_surface_find(state.hub,"ext4_fs.mutate");if(!stat || !read || !write || !mutate)return module_result(0,0);j->status=CB_OK;struct kernel_surface_result r;
    if(j->op==1){r=read(j->filesystem,(uintptr_t)j->path,j->offset,(uintptr_t)j->buffer,j->bytes,0);j->actual=r.value;j->status=file_status(r.status?file_error(j->filesystem):0);}
    else if(j->op==2){r=write(j->filesystem,(uintptr_t)j->path,j->offset,(uintptr_t)j->buffer,j->bytes,j->mode==CB_FILE_APPEND?EXT4_WRITE_APPEND:0);j->actual=r.value;j->status=file_status(r.status?file_error(j->filesystem):0);}
    else {if(j->mode!=CB_FILE_READ){struct ext4_info mount={0};kernel_surface_call get=kernel_surface_find(state.hub,"ext4_fs.info");if(!get || CALL(get,j->filesystem,&mount,0).status){j->status=CB_RESOURCE_FAILURE;return module_result(1,1);}if(mount.readonly){j->status=CB_READ_ONLY;return module_result(1,1);}}struct ext4_stat info;r=stat(j->filesystem,(uintptr_t)j->path,(uintptr_t)&info,0,0,0);if(j->mode==CB_FILE_READ){j->status=file_status(r.status?file_error(j->filesystem):0);if(!j->status && (info.mode&0170000)!=0100000)j->status=CB_BAD_REQUEST;}
        else {if(r.status && file_error(j->filesystem)==EXT4_NOT_FOUND)r=mutate(j->filesystem,EXT4_MUTATE_CREATE,(uintptr_t)j->path,0,0,0);else if(!r.status && (info.mode&0170000)!=0100000)r.status=EXT4_TYPE;if(!r.status && j->mode==CB_FILE_RECREATE)r=mutate(j->filesystem,EXT4_MUTATE_TRUNCATE,(uintptr_t)j->path,0,0,0);j->status=file_status(r.status?file_error(j->filesystem):0);}}
    return module_result(1,1);
}
static cb_status_t file_run(struct file_job *j){
    kernel_surface_call create=kernel_surface_find(state.hub,"threads.create");if(!create)return CB_RESOURCE_FAILURE;
    struct thread_request request={.abi=1,.cpu=0,.stack_bytes=65536,.entry=(uintptr_t)kernel_surface_find(state.hub,"handles.file_worker"),.major=1,.payload={.arguments={(uintptr_t)j}}};
    struct kernel_surface_result t=CALL(create,&request,0,0);if(t.status)return CB_RESOURCE_FAILURE;if(CALL(state.control,THREAD_WAIT,t.value,0).status)__asm__ volatile("ud2");struct thread_info info;if(CALL(state.control,THREAD_INFO,t.value,&info).status || info.result.status || CALL(state.control,THREAD_REAP,t.value,0).status)return CB_RESOURCE_FAILURE;return j->status;
}
static cb_status_t stream_call(uint64_t pid,const struct cb_call_header *call,const void *payload,void *out,size_t *actual){
    uint64_t flags=lock();struct capability *c=capability(pid,call->handle);if(!c){unlock(flags);return CB_BAD_HANDLE;}
    if(call->operation==CB_STREAM_PIPE){
        if(c->stream || call->payload_bytes || !permitted(c->operations,"read",4) || !permitted(c->operations,"write",5)){unlock(flags);return CB_BAD_REQUEST;}struct stream *s=allocate(sizeof(*s)+CB_PIPE_CAPACITY);if(!s){unlock(flags);return CB_RESOURCE_FAILURE;}
        struct capability *reader=stream_endpoint(policy(pid),c->publication,s,CB_STREAM_READ);if(!reader){release(s);unlock(flags);return CB_RESOURCE_FAILURE;}struct capability *writer=stream_endpoint(policy(pid),c->publication,s,CB_STREAM_WRITE);if(!writer){drop_capability(reader);unlock(flags);return CB_RESOURCE_FAILURE;}
        struct cb_stream_pair pair={reader->id,writer->id};copy_bytes(out,&pair,sizeof(pair));*actual=sizeof(pair);unlock(flags);return CB_OK;
    }
    if(!c->stream || c->direction!=call->operation){unlock(flags);return CB_DENIED;}struct stream *s=c->stream;struct cb_stream_read read={0};size_t count=call->payload_bytes;uint64_t options=0;
    if(call->operation==CB_STREAM_READ){if(count!=sizeof(read)){unlock(flags);return CB_BAD_REQUEST;}copy_bytes(&read,payload,sizeof(read));if(read.bytes>CB_STREAM_CHUNK || (read.flags&~CB_STREAM_NONBLOCK)){unlock(flags);return CB_BAD_REQUEST;}count=read.bytes;options=read.flags;}
    else {if(count<8 || count-8>CB_STREAM_CHUNK){unlock(flags);return CB_BAD_REQUEST;}copy_bytes(&options,payload,8);if(options&~CB_STREAM_NONBLOCK){unlock(flags);return CB_BAD_REQUEST;}payload=(const unsigned char *)payload+8;count-=8;}
    struct stream_waiter waiter={.thread=CALL(state.control,THREAD_CURRENT,0,0).value};cb_status_t status=CB_OK;
    if(s->filesystem){
        while(s->busy){if(options&CB_STREAM_NONBLOCK){unlock(flags);return CB_WOULD_BLOCK;}waiter.next=s->waiters;s->waiters=&waiter;unlock(flags);status=sleep_until(call->deadline_ns);flags=lock();struct stream_waiter **at=&s->waiters;while(*at!=&waiter)at=&(*at)->next;*at=waiter.next;if(status){unlock(flags);return status;}}
        s->busy=1;struct file_job job={.filesystem=s->filesystem,.offset=s->offset,.path=s->path,.op=call->operation==CB_STREAM_READ?1:2,.mode=s->file_mode,.buffer=call->operation==CB_STREAM_READ?out:(void *)payload,.bytes=count};unlock(flags);status=file_run(&job);flags=lock();if(!status){s->offset+=job.actual;if(call->operation==CB_STREAM_READ)*actual=job.actual;else {uint64_t n=job.actual;copy_bytes(out,&n,8);*actual=8;}}s->busy=0;stream_wake(s);unlock(flags);return status;
    }

    for(;;){
        if(call->operation==CB_STREAM_READ){if(s->used || !s->writers || !count){if(count>s->used)count=s->used;for(size_t i=0;i<count;++i)((unsigned char *)out)[i]=s->data[(s->head+i)%CB_PIPE_CAPACITY];s->head=(s->head+count)%CB_PIPE_CAPACITY;s->used-=count;*actual=count;stream_wake(s);break;}}
        else {if(!s->readers){status=CB_BROKEN_PIPE;break;}if(s->used<CB_PIPE_CAPACITY || !count){if(count>CB_PIPE_CAPACITY-s->used)count=CB_PIPE_CAPACITY-s->used;for(size_t i=0;i<count;++i)s->data[(s->head+s->used+i)%CB_PIPE_CAPACITY]=((const unsigned char *)payload)[i];s->used+=count;uint64_t written=count;copy_bytes(out,&written,8);*actual=8;stream_wake(s);break;}}
        if(options&CB_STREAM_NONBLOCK){status=CB_WOULD_BLOCK;break;}
        waiter.next=s->waiters;s->waiters=&waiter;unlock(flags);status=sleep_until(call->deadline_ns);flags=lock();struct stream_waiter **at=&s->waiters;while(*at!=&waiter)at=&(*at)->next;*at=waiter.next;if(status)break;
    }
    unlock(flags);return status;
}
/* Trusted launch-time delegation: requires READY child and intersects its own
 * immutable manifest. Rollback drops all new endpoints before execution. */
struct kernel_surface_result handles_surface_stream_bind(MODULE_ARGS){
    MODULE_UNUSED();if(!c)return module_result(0,0);uint64_t flags=lock();struct policy *parent=policy(a),*child=policy(b);struct process_info info;cb_handle_t source[3];copy_bytes(source,(const void *)c,sizeof(source));
    if(!parent || !child || CALL(state.process_info,b,&info,0).status || info.state!=PROCESS_READY || info.thread){unlock(flags);return module_result(0,0);}
    struct capability *created[3]={0};int ok=1;
    for(unsigned i=0;i<3;++i){if(!source[i])continue;struct capability *from=capability(a,source[i]);struct grant *g=grant(child,CB_STREAM_NAME);unsigned direction=i?CB_STREAM_WRITE:CB_STREAM_READ;const char *op=i?"write":"read";
        if(!from || !from->stream || from->direction!=direction || !g || !permitted(g->operations,(const unsigned char *)op,length(op)) || child->standard[i]){ok=0;break;}
        created[i]=stream_endpoint(child,from->publication,from->stream,direction);if(!created[i]){ok=0;break;}
    }
    for(unsigned i=0;i<3;++i)if(created[i]){if(ok)child->standard[i]=created[i]->id;else drop_capability(created[i]);}
    unlock(flags);return module_result(ok,ok);
}

/* Trusted filesystem bridge; pathname validation and caller identity are supplied
 * by the catalog-bound provider. Endpoint authority is checked again here. */
struct kernel_surface_result handles_surface_file_open(MODULE_ARGS){
    MODULE_UNUSED();if(!a || !b || !c || d<CB_FILE_READ || d>CB_FILE_APPEND)return module_result(0,0);uint64_t flags=lock();struct policy *owner=policy(a);struct grant *g=grant(owner,CB_STREAM_NAME);unsigned direction=d==CB_FILE_READ?CB_STREAM_READ:CB_STREAM_WRITE;const char *op=direction==CB_STREAM_READ?"read":"write";struct publication *p=state.publications;while(p && p->kernel!=3)p=p->next;
    if(!owner || !g || !p || !permitted(g->operations,(const unsigned char *)op,length(op))){unlock(flags);return module_result(0,0);}struct stream *s=allocate(sizeof(*s));if(!s){unlock(flags);return module_result(0,0);}size_t bytes=length((const void *)c);s->path=allocate(bytes+1);if(!s->path){release(s);unlock(flags);return module_result(0,0);}copy_bytes(s->path,(const void *)c,bytes+1);s->filesystem=b;s->file_mode=d;struct capability *cap=stream_endpoint(owner,p,s,direction);if(!cap){release(s->path);release(s);unlock(flags);return module_result(0,0);}uint64_t id=cap->id;unlock(flags);
    struct file_job job={.filesystem=b,.path=s->path,.op=3,.mode=d};cb_status_t status=file_run(&job);if(status){flags=lock();drop_capability(cap);unlock(flags);return (struct kernel_surface_result){.value=0,.status=status};}return module_result(id,1);
}
