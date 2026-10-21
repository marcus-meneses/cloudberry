#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "../kmodules/shared/process.c"
static unsigned live;static int deny;
static struct kernel_surface_result allocator(MODULE_ARGS){MODULE_UNUSED();void *p=deny?NULL:calloc(b,c);if(p)++live;return module_result((uintptr_t)p,p!=NULL);}
static struct kernel_surface_result disposer(MODULE_ARGS){MODULE_UNUSED();if(b){assert(live);--live;free((void *)b);}return module_result(1,1);}
static struct kernel_surface_result cpu(MODULE_ARGS){MODULE_UNUSED();return module_result(0,1);}
int main(void){
    struct process root={.info={.id=1,.state=PROCESS_READY},.credentials={.generation=1}},child={.info={.id=2,.state=PROCESS_READY},.credentials={.generation=1}};root.next=&child;state.head=&root;state.hub=(const void *)1;state.alloc=allocator;state.free=disposer;state.cpu=cpu;
    struct {struct cb_credentials h;uint64_t groups[2];} input={.h={.uid=42,.gid=2,.groups=2},.groups={1,8}},output={0};
    assert(!process_surface_credentials(1,1,(uintptr_t)&input,sizeof(input),0,0).status);
    assert(!process_surface_credentials(2,2,1,0,0,0).status && live==2 && root.groups!=child.groups);
    assert(!process_surface_credentials(2,0,(uintptr_t)&output,sizeof(output),0,0).status && output.h.uid==42 && output.h.gid==2 && output.h.groups==2 && output.groups[1]==8);
    assert(process_surface_credentials(2,3,1,0,0,0).value && !process_surface_credentials(2,3,0,0,0,0).value);
    struct cb_credentials drop={.uid=99,.gid=3};assert(!process_surface_credentials(1,1,(uintptr_t)&drop,sizeof(drop),0,0).status && child.credentials.uid==42);
    deny=1;assert(process_surface_credentials(2,1,(uintptr_t)&input,sizeof(input),0,0).status && child.credentials.uid==42);deny=0;
    assert(process_surface_credentials(2,1,(uintptr_t)&input,sizeof(input)-1,0,0).status);
    child.info.state=PROCESS_RUNNING;assert(process_surface_credentials(2,2,1,0,0,0).status);
    assert(process_surface_credentials(999,0,(uintptr_t)&output,sizeof(output),0,0).status);
    root.info.state=PROCESS_RUNNING;child.info.state=PROCESS_READY;
    assert(!process_surface_session(1,1,0,0,0,0).status);
    assert(!process_surface_credentials(2,2,1,0,0,0).status && child.parent==1 && child.session==1);
    assert(process_surface_ancestor(1,2,0,0,0,0).value && !process_surface_ancestor(2,1,0,0,0,0).value);
    struct cb_session session;assert(!process_surface_session(2,0,(uintptr_t)&session,sizeof(session),0,0).status && session.id==1 && session.uid==99);
    assert(!process_surface_session(1,2,1,0,0,0).status && child.session_closed && root.session_closed);
    assert(process_surface_credentials(2,2,1,0,0,0).status);
    assert(!process_surface_ancestor(1,2,0,0,0,0).value && !process_surface_ancestor(2,2,0,0,0,0).value);
    release(root.groups);release(child.groups);assert(!live);puts("PASS process credentials: independent inheritance, group checks, replacement, invalid length, OOM and lifecycle denial");
}
