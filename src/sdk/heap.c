#include "heap.h"
#define ALIGNMENT 16u
#define GRANULE 65536u
struct block {struct block *next,*previous;size_t capacity,requested,offset,alignment;};
struct cb_heap_arena {struct cb_heap_arena *next;size_t bytes,live,reserved;struct block first;};
_Static_assert(sizeof(struct block)%ALIGNMENT==0 && offsetof(struct cb_heap_arena,first)%ALIGNMENT==0,"heap alignment");
static int rounded(size_t n,size_t alignment,size_t *out){if(n>SIZE_MAX-(alignment-1))return 0;*out=(n+alignment-1)&~(alignment-1);return 1;}
void cb_heap_init(struct cb_heap *h,void *context,cb_heap_acquire_fn acquire,cb_heap_release_fn release){*h=(struct cb_heap){.context=context,.acquire=acquire,.release=release};}
static struct block *lookup(struct cb_heap *h,const void *p,struct cb_heap_arena **owner){for(struct cb_heap_arena *a=h->arenas;a;a=a->next)for(struct block *b=&a->first;b;b=b->next)if((unsigned char *)(b+1)+b->offset==p && b->requested){if(owner)*owner=a;return b;}return NULL;}
static void split(struct block *b,size_t bytes){
    if(b->capacity-bytes<sizeof(struct block)+ALIGNMENT)return;
    struct block *n=(void *)((unsigned char *)(b+1)+bytes);*n=(struct block){.next=b->next,.previous=b,.capacity=b->capacity-bytes-sizeof(*n)};
    if(n->next)n->next->previous=n;
    b->next=n;b->capacity=bytes;
}
static void merge(struct block *b){struct block *n=b->next;if(n && !n->requested){b->capacity+=sizeof(*n)+n->capacity;b->next=n->next;if(b->next)b->next->previous=b;}}
void *cb_heap_alloc(struct cb_heap *h,size_t bytes){
    if(!h || !h->acquire || !h->release || !bytes)return NULL;
    size_t capacity;if(!rounded(bytes,ALIGNMENT,&capacity) || bytes>SIZE_MAX-h->used || h->allocations==SIZE_MAX)return NULL;
    struct cb_heap_arena *a;struct block *b=NULL;
    for(a=h->arenas;a;a=a->next){for(b=&a->first;b;b=b->next)if(!b->requested && b->capacity>=capacity)break;if(b)break;}
    if(!a){size_t size;if(capacity>SIZE_MAX-sizeof(*a) || !rounded(capacity+sizeof(*a),4096,&size))return NULL;if(size<GRANULE)size=GRANULE;if(size>SIZE_MAX-h->mapped)return NULL;
        a=h->acquire(h->context,size);if(!a)return NULL;
        *a=(struct cb_heap_arena){.next=h->arenas,.bytes=size,.first={.capacity=size-sizeof(*a)}};h->arenas=a;h->mapped+=size;b=&a->first;
    }
    split(b,capacity);b->requested=bytes;b->alignment=ALIGNMENT;b->offset=0;++a->live;++h->allocations;h->used+=bytes;return b+1;
}
void *cb_heap_aligned(struct cb_heap *h,size_t alignment,size_t bytes){if(!alignment || (alignment&(alignment-1)) || !bytes)return NULL;if(alignment<=ALIGNMENT)return cb_heap_alloc(h,bytes);if(bytes>SIZE_MAX-(alignment-1))return NULL;size_t total=bytes+alignment-1;void *raw=cb_heap_alloc(h,total);if(!raw)return NULL;struct block *b=lookup(h,raw,NULL);b->offset=(-((uintptr_t)raw))&(alignment-1);b->alignment=alignment;b->requested=bytes;h->used-=total-bytes;return (unsigned char *)raw+b->offset;}
void *cb_heap_calloc(struct cb_heap *h,size_t count,size_t bytes){if(bytes && count>SIZE_MAX/bytes)return NULL;size_t n=count*bytes;unsigned char *p=cb_heap_alloc(h,n);if(p)for(size_t i=0;i<n;++i)p[i]=0;return p;}
static int release_empty(struct cb_heap *h,struct cb_heap_arena *a){
    if(a->live)return 0;
    struct cb_heap_arena *next=a->next;size_t bytes=a->bytes;struct cb_heap_arena **link=&h->arenas;while(*link!=a)link=&(*link)->next;
    if(!h->release(h->context,a,bytes))return 0;
    *link=next;h->mapped-=bytes;return 1;
}
int cb_heap_free(struct cb_heap *h,void *p){
    if(!p)return 1;
    if(!h)return 0;
    struct cb_heap_arena *a;struct block *b=lookup(h,p,&a);if(!b)return 0;
    h->used-=b->requested;--h->allocations;--a->live;b->requested=0;b->offset=0;merge(b);if(b->previous && !b->previous->requested)merge(b->previous);
    if(!a->live)(void)release_empty(h,a);
    return 1;
}
size_t cb_heap_usable(struct cb_heap *h,const void *p){struct block *b=h?lookup(h,p,NULL):NULL;return b?b->capacity-b->offset:0;}
void *cb_heap_realloc(struct cb_heap *h,void *p,size_t bytes){
    if(!p)return cb_heap_alloc(h,bytes);
    if(!bytes){(void)cb_heap_free(h,p);return NULL;}
    struct block *b=h?lookup(h,p,NULL):NULL;size_t capacity;if(!b || bytes>SIZE_MAX-b->offset || !rounded(bytes+b->offset,ALIGNMENT,&capacity))return NULL;
    size_t old=b->requested;if(bytes>old && bytes-old>SIZE_MAX-h->used)return NULL;
    if(b->capacity<capacity && b->next && !b->next->requested && b->capacity+sizeof(*b)+b->next->capacity>=capacity)merge(b);
    if(b->capacity>=capacity){split(b,capacity);if(b->next)merge(b->next);b->requested=bytes;h->used=h->used-old+bytes;return p;}
    void *n=cb_heap_aligned(h,b->alignment,bytes);if(!n)return NULL;
    /* Usable-size clients (including embedded runtimes) may use all reported
     * capacity. Preserve that capacity on a moving growth, not just the last
     * requested count. This path necessarily grows beyond the old capacity. */
    for(size_t i=0;i<b->capacity-b->offset;++i)((unsigned char *)n)[i]=((unsigned char *)p)[i];
    (void)cb_heap_free(h,p);return n;
}
int cb_heap_destroy(struct cb_heap *h){if(!h || h->allocations)return 0;for(struct cb_heap_arena *a=h->arenas,*next;a;a=next){next=a->next;if(!release_empty(h,a))return 0;}return 1;}
