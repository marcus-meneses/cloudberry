/* Real provider transaction/recovery code on a durable in-memory filesystem.
 * Crashes discard every provider allocation and kernel staging snapshot. */
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void fault(unsigned point);
#define CB_INSTALL_FAULT(point) fault(point)
#define _start installer_entry
#include "../handlers/installer/installer.c"
#undef _start
struct allocation {struct allocation *next;void *p;};static struct allocation *allocations;
struct file {char *path;unsigned char *data;size_t bytes;unsigned directory;uint64_t uid,gid,mode;};static struct file files[256];static size_t file_count;
struct opened {struct file *file;size_t offset;};static struct opened opened[2048];static size_t open_count;
static unsigned crash_at;static jmp_buf crash;
static unsigned char *staged;static size_t staged_bytes,staged_at;static uint64_t kernel_generation=1,serial,staging_token;
static void fault(unsigned point){if(crash_at==point)longjmp(crash,1);}
static struct file *find_file(const char *path){for(size_t i=0;i<file_count;++i)if(files[i].path && !strcmp(files[i].path,path))return &files[i];return NULL;}
static struct file *create_file(const char *path,unsigned dir){struct file *f=find_file(path);if(f)return f;assert(file_count<256);f=&files[file_count++];*f=(struct file){.path=strdup(path),.directory=dir,.mode=dir?0700:0600};assert(f->path);return f;}
static void disk_write(const char *path,const void *data,size_t bytes){struct file *f=create_file(path,0);free(f->data);f->data=bytes?malloc(bytes):NULL;assert(!bytes || f->data);if(bytes)memcpy(f->data,data,bytes);f->bytes=bytes;}
static void *user_allocate(size_t bytes){struct allocation *a=malloc(sizeof(*a));assert(a);a->p=calloc(1,bytes?bytes:1);assert(a->p);a->next=allocations;allocations=a;return a->p;}
static void user_release(void *p){struct allocation **at=&allocations;while(*at && (*at)->p!=p)at=&(*at)->next;assert(*at);struct allocation *a=*at;*at=a->next;free(a->p);free(a);}
static void reboot(void){while(allocations)user_release(allocations->p);free(staged);staged=NULL;staged_bytes=staged_at=0;kernel_generation=1;serial=staging_token=0;registrations=NULL;rejected=NULL;registration_bytes=0;failed=0;open_count=0;memset(&client,0,sizeof(client));memset(&responder,0,sizeof(responder));}
static void reset_disk(void){reboot();for(size_t i=0;i<file_count;++i){free(files[i].path);free(files[i].data);}file_count=0;unsigned char empty[96];cb_reg_put(empty,CB_REGISTRY_MAGIC);cb_reg_put(empty+8,32);cb_reg_put(empty+16,1);cb_reg_put(empty+24,0);crypto_sha512(empty+32,empty,32);disk_write(REGISTRY_PATH,empty,96);disk_write(JOURNAL_PATH,NULL,0);}
cb_status_t cb_system_invoke(const void *input,uint64_t bytes,void *output,uint64_t capacity,uint64_t *actual){
    struct cb_call_header c;memcpy(&c,input,sizeof(c));assert(bytes==sizeof(c)+c.payload_bytes);const unsigned char *p=(const unsigned char *)input+sizeof(c);struct cb_reply_header h={.version={sizeof(h),1,0},.provider_epoch=1};unsigned char result[CB_FRAME_LIMIT];
    if(c.handle==1){struct cb_memory_request r;memcpy(&r,p,sizeof(r));struct cb_memory_result out={0};if(c.operation==CB_MEMORY_ALLOCATE){out.address=(uintptr_t)user_allocate(r.bytes);out.bytes=r.bytes;}else{assert(c.operation==CB_MEMORY_FREE);user_release((void *)(uintptr_t)r.address);}memcpy(result,&out,sizeof(out));h.result_bytes=sizeof(out);}
    else if(c.handle==2){struct cb_filesystem_request r;memcpy(&r,p,sizeof(r));char path[4096];assert(r.path_bytes<=4095);memcpy(path,p+sizeof(r),r.path_bytes);path[r.path_bytes]=0;struct file *f=find_file(path);
        if(c.operation==CB_FILESYSTEM_STAT || c.operation==CB_FILESYSTEM_METADATA){if(!f)h.status=CB_NOT_FOUND;else if(c.operation==CB_FILESYSTEM_STAT){struct cb_filesystem_stat out={f->bytes,f->directory,0};memcpy(result,&out,sizeof(out));h.result_bytes=sizeof(out);}else{struct cb_filesystem_metadata out={f->bytes,f->uid,f->gid,f->mode,f->directory};memcpy(result,&out,sizeof(out));h.result_bytes=sizeof(out);}}
        else if(c.operation==CB_FILESYSTEM_OPEN){if(r.mode==CB_FILE_READ && !f)h.status=CB_NOT_FOUND;else {if(!f)f=create_file(path,0);assert(!f->directory && open_count<2048);if(r.mode==CB_FILE_RECREATE)disk_write(path,NULL,0);size_t slot=++open_count;opened[slot-1]=(struct opened){f,r.mode==CB_FILE_APPEND?f->bytes:0};uint64_t cap=1000+slot;memcpy(result,&cap,8);h.result_bytes=8;}}
        else if(c.operation==CB_FILESYSTEM_MKDIR){if(!f)create_file(path,1);else h.status=CB_BUSY;}
        else if(c.operation==CB_FILESYSTEM_CHMOD){if(!f)h.status=CB_NOT_FOUND;else f->mode=r.mode;}
        else if(c.operation==CB_FILESYSTEM_CHOWN){if(!f)h.status=CB_NOT_FOUND;else{struct cb_filesystem_owner owner;memcpy(&owner,p+sizeof(r)+r.path_bytes,sizeof(owner));f->uid=owner.uid;f->gid=owner.gid;}}
        else if(c.operation==CB_FILESYSTEM_LIST){if(!f)h.status=CB_NOT_FOUND;else if(r.cursor<2){struct cb_filesystem_entry out={.cursor=r.cursor+1,.directory=1,.name_bytes=r.cursor?2:1};memcpy(out.name,"..",out.name_bytes);memcpy(result,&out,sizeof(out));h.result_bytes=sizeof(out);}else {size_t n=strlen(path);for(size_t i=0;i<file_count;++i)if(files[i].path && !strncmp(files[i].path,path,n) && files[i].path[n]=='/' && !strchr(files[i].path+n+1,'/')){struct cb_filesystem_entry out={.cursor=r.cursor+1,.directory=files[i].directory,.name_bytes=strlen(files[i].path+n+1)};memcpy(out.name,files[i].path+n+1,out.name_bytes);memcpy(result,&out,sizeof(out));h.result_bytes=sizeof(out);break;}}}
        else {assert(c.operation==CB_FILESYSTEM_REMOVE || c.operation==CB_FILESYSTEM_RMDIR);if(!f)h.status=CB_NOT_FOUND;else if((f->directory && c.operation!=CB_FILESYSTEM_RMDIR) || (!f->directory && c.operation==CB_FILESYSTEM_RMDIR))h.status=CB_BAD_REQUEST;else{size_t n=strlen(path);for(size_t i=0;i<file_count;++i)if(files[i].path && !strncmp(files[i].path,path,n) && files[i].path[n]=='/')h.status=CB_BUSY;if(!h.status){free(f->path);free(f->data);*f=(struct file){0};}}}
    }else if(c.handle>=1001){size_t slot=c.handle-1001;assert(slot<open_count);struct opened *o=&opened[slot];if(c.operation==CB_STREAM_READ){struct cb_stream_read r;memcpy(&r,p,sizeof(r));size_t n=o->file->bytes-o->offset;if(n>r.bytes)n=r.bytes;if(n)memcpy(result,o->file->data+o->offset,n);o->offset+=n;h.result_bytes=n;}else{assert(c.operation==CB_STREAM_WRITE && c.payload_bytes>=sizeof(struct cb_stream_write));size_t bytes_=c.payload_bytes-sizeof(struct cb_stream_write),n=o->offset+bytes_;if(n>o->file->bytes){o->file->data=realloc(o->file->data,n);assert(o->file->data);o->file->bytes=n;}memcpy(o->file->data+o->offset,p+sizeof(struct cb_stream_write),bytes_);o->offset=n;uint64_t accepted=bytes_;memcpy(result,&accepted,8);h.result_bytes=8;}}
    else {assert(c.handle==3 && c.operation==CB_IO_REGISTRY);struct cb_registry_request r;memcpy(&r,p,sizeof(r));if(r.operation==CB_REGISTRY_VERSION){memcpy(result,&kernel_generation,8);h.result_bytes=8;}else if(r.operation==CB_REGISTRY_BEGIN){assert(!staged);staged=calloc(1,r.bytes);assert(staged);staged_bytes=r.bytes;staged_at=0;staging_token=++serial;memcpy(result,&staging_token,8);h.result_bytes=8;}else if(r.operation==CB_REGISTRY_STAGE){assert(r.token==staging_token && r.offset==staged_at && c.payload_bytes-sizeof(r)<=staged_bytes-staged_at);memcpy(staged+staged_at,p+sizeof(r),c.payload_bytes-sizeof(r));staged_at+=c.payload_bytes-sizeof(r);}else if(r.operation==CB_REGISTRY_PREPARE){assert(staged && staged_at==staged_bytes);}else if(r.operation==CB_REGISTRY_COMMIT){assert(staged && r.token==staging_token);kernel_generation=cb_reg_u64(staged+16);free(staged);staged=NULL;}else if(r.operation==CB_REGISTRY_LIST){}else{assert(r.operation==CB_REGISTRY_ABORT);free(staged);staged=NULL;}}
    assert(sizeof(h)+h.result_bytes<=capacity);memcpy(output,&h,sizeof(h));if(h.result_bytes)memcpy((unsigned char *)output+sizeof(h),result,h.result_bytes);*actual=sizeof(h)+h.result_bytes;return h.status;
}
cb_status_t cb_system_release(cb_handle_t h){(void)h;return CB_OK;}
cb_status_t cb_system_request_release(struct cb_request_id id){(void)id;abort();}
__attribute__((noreturn)) void cb_exit(uint64_t code){fprintf(stderr,"unexpected exit %llu\n",(unsigned long long)code);abort();}
static struct blob read_host(const char *path){FILE *f=fopen(path,"rb");assert(f);fseek(f,0,SEEK_END);size_t n=ftell(f);rewind(f);unsigned char *p=malloc(n);assert(p && fread(p,1,n,f)==n);fclose(f);return (struct blob){p,n};}
static void setup_action(struct action *a,const struct blob *package,unsigned remove){memset(a,0,sizeof(*a));a->owner=CB_REGISTRY_SYSTEM;a->operation=remove?2:1;a->options=remove?CB_INSTALL_DELETE_DATA:0;struct blob copy=*package;assert(verify(&copy,&a->bundle)==CB_OK);char leaf[256];static const char hex[]="0123456789abcdef";leaf[0]='_';for(unsigned i=0;i<16;++i){leaf[1+i*2]=hex[package->data[64+i]>>4];leaf[2+i*2]=hex[package->data[64+i]&15];}leaf[33]='.';memcpy(leaf+34,package->data+112,a->bundle.name);leaf[34+a->bundle.name]=0;strcpy(a->base,"/system/apps/");strcat(a->base,leaf);struct data_plan *plan=user_allocate(sizeof(*plan)+13);*plan=(struct data_plan){.uid=0,.gid=0,.existed=remove};strcpy(plan->home,"/system/root");a->plans=plan;struct cb_install_record r={0};size_t old=remove?installed(CB_REGISTRY_SYSTEM,a->bundle.data+112,a->bundle.name,&r):SIZE_MAX;assert(new_registry(a,old,&r)==CB_OK);}
int main(int argc,char **argv){assert(argc==4);struct blob upgraded=read_host(argv[3]);struct blob key=read_host(argv[1]),package=read_host(argv[2]);assert(key.bytes==32);memcpy(trusted_key,key.data,32);memory=1;filesystem=2;io=3;
    static unsigned removal,point;for(removal=0;removal<2;++removal)for(point=1;point<=5;++point){reset_disk();assert(load_store()==CB_OK);struct action *a=calloc(1,sizeof(*a));assert(a);setup_action(a,&package,0);uint64_t token;assert(stage_registry(a->next,a->next_bytes,2,0,&token)==CB_OK);crash_at=removal?0:point;if(!setjmp(crash)){assert(commit_action(a,token)==CB_OK);assert(removal);}if(removal){free_plans(a);release(a->next);setup_action(a,&package,1);assert(stage_registry(a->next,a->next_bytes,2,0,&token)==CB_OK);crash_at=point;if(!setjmp(crash)){(void)commit_action(a,token);abort();}}
        char base[4096],private[4096];strcpy(base,a->base);assert(data_path(private,"/system/root",base)==CB_OK);reboot();crash_at=0;assert(load_store()==CB_OK);unsigned installed_now=removal?point<3:point>=3;assert(kernel_generation==(removal?(point<3?2:3):(point<3?1:2)));assert((find_file(base)!=NULL)==installed_now);assert((find_file(private)!=NULL)==installed_now);assert(find_file(JOURNAL_PATH)->bytes==0);free(a);
    }
    static unsigned upgrade_cut;for(upgrade_cut=1;upgrade_cut<=6;++upgrade_cut){
        reset_disk();assert(load_store()==CB_OK);struct action *a=calloc(1,sizeof(*a));assert(a);setup_action(a,&package,0);uint64_t token;assert(stage_registry(a->next,a->next_bytes,2,0,&token)==CB_OK);assert(commit_action(a,token)==CB_OK);free_plans(a);
        char private[4096],state[4096];assert(data_path(private,"/system/root",a->base)==CB_OK && suffix(state,private,"/state")==CB_OK);disk_write(state,"KEEP",4);
        setup_action(a,&upgraded,0);a->operation=3;a->plans->existed=1;a->previous=(struct blob){user_allocate(package.bytes),package.bytes};memcpy(a->previous.data,package.data,package.bytes);release(a->next);struct cb_install_record old;size_t index=installed(a->owner,a->bundle.data+112,a->bundle.name,&old);assert(index!=SIZE_MAX && new_registry(a,index,&old)==CB_OK);
        assert(stage_registry(a->next,a->next_bytes,2,0,&token)==CB_OK);crash_at=upgrade_cut;
        if(!setjmp(crash)){(void)commit_action(a,token);abort();}
        reboot();crash_at=0;assert(load_store()==CB_OK && !failed);struct blob *expected=upgrade_cut<3?&package:&upgraded;
        struct file *code=find_file(a->base);uint64_t nb=cb_reg_u64(expected->data+16),mb=cb_reg_u64(expected->data+24),cb=cb_reg_u64(expected->data+32);assert(code && code->bytes==cb && !memcmp(code->data,expected->data+112+nb+mb,cb));
        struct file *kept=find_file(state);assert(kept && kept->bytes==4 && !memcmp(kept->data,"KEEP",4));assert(!find_file(JOURNAL_PATH)->bytes);free(a);
    }
    /* A committed intent survives truncation or a torn registry rewrite. */
    static unsigned damaged;for(damaged=0;damaged<2;++damaged){
        reset_disk();assert(load_store()==CB_OK);struct action *a=calloc(1,sizeof(*a));assert(a);setup_action(a,&package,0);uint64_t token;assert(stage_registry(a->next,a->next_bytes,2,0,&token)==CB_OK);crash_at=3;
        if(!setjmp(crash)){(void)commit_action(a,token);abort();}
        disk_write(REGISTRY_PATH,damaged?"bad":"",damaged?3:0);reboot();crash_at=0;assert(load_store()==CB_OK && kernel_generation==2 && find_file(JOURNAL_PATH)->bytes==0);free(a);
    }
    reset_disk();disk_write(REGISTRY_PATH,"bad",3);assert(initialize_store()!=CB_OK && failed && cb_reg_u64(registrations+24)==0);
    reset_disk();disk_write(REGISTRY_PATH,"bad",3);disk_write(JOURNAL_PATH,"partial",7);assert(initialize_store()!=CB_OK && find_file(JOURNAL_PATH)->bytes==7);
    reset_disk();assert(load_store()==CB_OK);struct action isolated;setup_action(&isolated,&package,0);uint64_t isolated_token;assert(stage_registry(isolated.next,isolated.next_bytes,2,0,&isolated_token)==CB_OK);assert(commit_action(&isolated,isolated_token)==CB_OK);disk_write(isolated.base,"damaged",7);free_plans(&isolated);reboot();assert(initialize_store()==CB_OK && failed && rejected && cb_reg_u64(registrations+24)==1);

    reset_disk();assert(load_store()==CB_OK);struct action a;setup_action(&a,&package,0);struct blob journal;assert(intent(&a,&journal)==CB_OK);disk_write(JOURNAL_PATH,journal.data,journal.bytes/2);reboot();assert(load_store()==CB_OK && kernel_generation==1);reset_disk();free(key.data);free(package.data);free(upgraded.data);puts("PASS installer recovery: install/remove at five and upgrade at six crash boundaries, code/resources rollback and private-data preservation, private-data cleanup, signed bytes, resource trees, durable registration and incomplete intent");
}
