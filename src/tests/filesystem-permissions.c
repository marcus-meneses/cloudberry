#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../kmodules/shared/filesystem.c"
static struct cb_credentials caller={.uid=42,.gid=2};static unsigned mode=0700,group,missing,valid=1,rootmode=0777;
static struct kernel_surface_result credentials(MODULE_ARGS){MODULE_UNUSED();if(a!=9)return module_result(0,0);if(!b){*(struct cb_credentials *)c=caller;((struct cb_credentials *)c)->groups=group?1:0;if(group && d>=sizeof(caller)+8)*(uint64_t *)(c+sizeof(caller))=8;return module_result(sizeof(caller)+(group?8:0),1);}return module_result(caller.gid==c || (group && c==8),1);}
static struct kernel_surface_result backend(MODULE_ARGS){MODULE_UNUSED();struct filesystem_request *r=(void *)b;r->status=CB_OK;if(!strcmp(r->path,"/")){r->stat=(struct filesystem_stat){.uid=0,.gid=0,.permissions=rootmode,.valid=valid,.type=FS_DIRECTORY};}else if(!strcmp(r->path,"/private")){r->stat=(struct filesystem_stat){.uid=42,.gid=8,.permissions=mode,.valid=valid,.type=FS_DIRECTORY};}else if(!strcmp(r->path,"/private/file")){if(missing)r->status=CB_NOT_FOUND;else r->stat=(struct filesystem_stat){.uid=42,.gid=8,.permissions=0600,.valid=valid,.type=FS_REGULAR};}else r->status=CB_NOT_FOUND;return module_result(1,1);}
static struct kernel_surface_result cpu_fixture(MODULE_ARGS){MODULE_UNUSED();return module_result(0,1);}
static struct kernel_surface_result allocate_fixture(MODULE_ARGS){MODULE_UNUSED();void *p=calloc(b,c);return module_result((uintptr_t)p,p!=NULL);}
static struct kernel_surface_result free_fixture(MODULE_ARGS){MODULE_UNUSED();free((void *)b);return module_result(1,1);}
static struct kernel_surface_result active_policy(MODULE_ARGS){MODULE_UNUSED();return module_result(1,1);}
static struct kernel_surface_result application_policy(MODULE_ARGS){MODULE_UNUSED();return module_result(CB_OK,1);}
int main(void){
    struct {struct kernel_surfaces header;struct kernel_surface entries[3];} hub={0};hub.header.major=3;hub.header.count=3;hub.header.methods_address=(uintptr_t)hub.entries;hub.entries[0].call=credentials;strcpy(hub.entries[0].name,"process.credentials");strcpy(hub.entries[1].name,"applications.access");hub.entries[1].call=application_policy;strcpy(hub.entries[2].name,"process.active");hub.entries[2].call=active_policy;fs.hub=&hub.header;fs.alloc=allocate_fixture;fs.free=free_fixture;
    struct backend format={.api={.invoke=backend}};struct mount mount={.backend=&format,.path="/"};struct filesystem_request r={.process=9,.path="/private/file",.operation=FS_READ};
    assert(access_file(&mount,&r)==CB_OK);caller.uid=43;assert(access_file(&mount,&r)==CB_DENIED);mode=0750;group=1;assert(access_file(&mount,&r)==CB_DENIED);r.path="/private";r.operation=FS_LIST;assert(access_file(&mount,&r)==CB_OK);group=0;assert(access_file(&mount,&r)==CB_DENIED);
    caller.uid=42;r.path="/private/file";r.operation=FS_PREPARE;r.mode=CB_FILE_APPEND;missing=1;assert(access_file(&mount,&r)==CB_OK && r.uid==42 && r.gid==2);mode=0500;assert(access_file(&mount,&r)==CB_DENIED);mode=0700;rootmode=0700;assert(access_file(&mount,&r)==CB_DENIED);rootmode=0777;valid=0;assert(access_file(&mount,&r)==CB_DENIED);caller.uid=0;assert(access_file(&mount,&r)==CB_OK);
    caller.uid=42;valid=1;mode=0700;rootmode=0700;fs.cpu=cpu_fixture;struct mount parent={.id=1,.path="/",.backend=&format};fs.mounts=&parent;mount.path="/mounted";r.operation=FS_LIST;r.path="/private";assert(access_file(&mount,&r)==CB_DENIED);rootmode=0777;assert(access_file(&mount,&r)==CB_OK && !parent.refs);mount.path="/";
    rootmode=0777;mode=0700;caller.uid=42;r.path="/private/file";missing=0;
    r.operation=FS_CHMOD;r.mode=0640;assert(access_file(&mount,&r)==CB_OK);caller.uid=43;assert(access_file(&mount,&r)==CB_DENIED);
    caller.uid=42;r.operation=FS_CHOWN;r.uid=99;r.gid=8;assert(access_file(&mount,&r)==CB_DENIED);
    caller.uid=0;r.uid=UINT64_MAX;assert(access_file(&mount,&r)==CB_DENIED);r.uid=99;assert(access_file(&mount,&r)==CB_OK && r.uid==99 && r.gid==8);
    r.operation=FS_SYNC;r.path=NULL;assert(access_file(&mount,&r)==CB_DENIED);
    puts("PASS filesystem permissions: traversal, owner/group/other, creation parent access, missing metadata and root DAC bypass");
}
