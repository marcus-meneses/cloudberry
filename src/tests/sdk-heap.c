#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "../sdk/heap.h"
static int budget=-1,release_failure;static size_t live,acquisitions;
static void *acquire(void *ctx,size_t n){(void)ctx;if(!budget)return NULL;if(budget>0)--budget;void *p=aligned_alloc(4096,n);if(p){++live;++acquisitions;memset(p,0xa5,n);}return p;}
static int release(void *ctx,void *p,size_t n){(void)ctx;(void)n;if(release_failure)return 0;assert(live);--live;free(p);return 1;}
int main(void){struct cb_heap h;cb_heap_init(&h,NULL,acquire,release);
    void *items[1000];for(unsigned i=0;i<1000;++i){items[i]=cb_heap_alloc(&h,17);assert(items[i] && !((uintptr_t)items[i]&15));memset(items[i],i,17);}assert(acquisitions<4);
    for(unsigned i=0;i<1000;i+=2)assert(cb_heap_free(&h,items[i]));
    for(unsigned i=1;i<1000;i+=2){for(unsigned j=0;j<17;++j)assert(((unsigned char *)items[i])[j]==(unsigned char)i);assert(cb_heap_free(&h,items[i]));}assert(!live && !h.used && !h.mapped && !h.allocations);
    unsigned char *p=cb_heap_alloc(&h,100),*block=cb_heap_alloc(&h,100);memset(p,42,100);budget=0;assert(!cb_heap_realloc(&h,p,100000));for(unsigned i=0;i<100;++i)assert(p[i]==42);assert(!cb_heap_calloc(&h,SIZE_MAX,2) && !cb_heap_alloc(&h,SIZE_MAX));budget=-1;
    assert(cb_heap_free(&h,block));assert(cb_heap_realloc(&h,p,200)==p);for(unsigned i=0;i<100;++i)assert(p[i]==42);assert(cb_heap_realloc(&h,p,16)==p);assert(cb_heap_usable(&h,p)>=16);
    release_failure=1;assert(cb_heap_free(&h,p));assert(live==1 && !h.allocations && !cb_heap_destroy(&h));release_failure=0;assert(cb_heap_destroy(&h) && !live);
    p=cb_heap_alloc(&h,19);size_t usable=cb_heap_usable(&h,p);memset(p,77,usable);block=cb_heap_alloc(&h,19);unsigned char *moved=cb_heap_realloc(&h,p,100000);assert(moved && moved!=p);for(size_t i=0;i<usable;++i)assert(moved[i]==77);assert(cb_heap_free(&h,moved) && cb_heap_free(&h,block) && !live);
    p=cb_heap_calloc(&h,99,3);for(unsigned i=0;i<297;++i)assert(!p[i]);assert(!cb_heap_destroy(&h));assert(cb_heap_realloc(&h,p,0)==NULL);assert(!live);
    for(size_t alignment=1;alignment<=1048576;alignment*=2){p=cb_heap_aligned(&h,alignment,37);assert(p && !((uintptr_t)p&(alignment-1)));memset(p,0x5a,cb_heap_usable(&h,p));block=cb_heap_alloc(&h,64);moved=cb_heap_realloc(&h,p,alignment+100000);assert(moved && !((uintptr_t)moved&(alignment-1)));for(unsigned i=0;i<37;++i)assert(moved[i]==0x5a);assert(cb_heap_free(&h,moved) && cb_heap_free(&h,block) && !live);}
    assert(!cb_heap_aligned(&h,3,20) && !cb_heap_aligned(&h,(SIZE_MAX/2)+1,SIZE_MAX));
    srand(3);memset(items,0,sizeof(items));for(unsigned i=0;i<20000;++i){unsigned at=rand()%1000;if(items[at]){assert(cb_heap_free(&h,items[at]));items[at]=NULL;}else items[at]=cb_heap_alloc(&h,1+rand()%8192);}for(unsigned i=0;i<1000;++i)assert(cb_heap_free(&h,items[i]));assert(cb_heap_destroy(&h) && !live);
    puts("PASS SDK heap: suballocation/alignment, fragmentation, OOM/overflow, realloc preservation, zeroing and backing-release ownership");}
