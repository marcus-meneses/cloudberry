#include "../kmodules/shared/bootstrap-load.c"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned live;static int budget=-1;
static struct kernel_surface_result alloc(MODULE_ARGS){MODULE_UNUSED();if(!budget)return module_result(0,0);if(budget>0)--budget;void *p=calloc(b,c);if(p)++live;return module_result((uintptr_t)p,p!=NULL);}
static struct kernel_surface_result drop(MODULE_ARGS){MODULE_UNUSED();if(b){assert(live);--live;free((void *)b);}return module_result(1,1);}
static int check(const char *text){char *copy=malloc(strlen(text)+1);strcpy(copy,text);struct manifest m={0};int ok=parse(&m,copy,strlen(text),"example");release(m.requires);free(copy);assert(!live);return ok;}
int main(void){
    state.alloc=alloc;state.free=drop;state.heap=1;
    const char *valid="version=1\nmodule=example\ninit=example.init\nstart=example.start\nrequires=clock.now_ns@1.0\npci.class=010802\n";
    assert(check(valid));assert(check("# comment\r\n version = 1 \r\nmodule=example\ninit=example.init\nmatch=always"));
    const char *bad[]={"", "version=2\nmodule=example\ninit=example.init\nmatch=always", "version=1\nmodule=other\ninit=example.init\nmatch=always", "version=1\nmodule=example\ninit=example.init\nmatch=always\nversion=1", "version=1\nmodule=example\ninit=example.init\nmatch=always\npci.class=010802", "version=1\nmodule=example\ninit=example.init\nmatch=always\nrequires=clock.now_ns@0.0", "version=1\nmodule=example\ninit=example.init\nmatch=always\nrequires=clock.now_ns@1.65536", "version=1\nmodule=example\ninit=example.init\nmatch=always\nrequires=clock.now_ns@1.0\nrequires=clock.now_ns@1.0", "version=1\nmodule=example\ninit=example.init\npci.class=gg0802", "version=1\nmodule=example\ninit=example.init\nmatch=always\nunknown=yes"};
    for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);++i)assert(!check(bad[i]));
    char many[4096]="version=1\nmodule=example\ninit=example.init\nmatch=always\n";for(unsigned i=0;i<40;++i){char line[64];snprintf(line,sizeof(line),"requires=method.%u@1.0\n",i);strcat(many,line);}assert(check(many));
    for(int n=0;n<5;++n){budget=n;assert(!check(many));}budget=-1;
    char embedded[]="version=1\0module=example";struct manifest m={0};assert(!parse(&m,embedded,sizeof(embedded)-1,"example") && !live);
    puts("PASS driver manifests: strict schema, versions, duplicates, PCI selectors, 40 dependencies, NUL rejection and allocation failure cleanup");
}
