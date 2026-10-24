#include "engine.h"
#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>
/* Private header: reported usable bytes never exceed requested, owned storage.
 * Track runtime metadata as well as GC objects; no second allocator/OS bypass. */
union block {struct {size_t bytes;} h;max_align_t alignment;};
static void *allocate(JSMallocState *s,size_t n){
    struct cb_js_allocator *a=s->opaque;
    if(!n || n>SIZE_MAX-sizeof(union block) || s->malloc_size>s->malloc_limit || n+sizeof(union block)>s->malloc_limit-s->malloc_size)return NULL;
    if(a->fail_after && ++a->attempts>=a->fail_after)return NULL;
    union block *p=malloc(sizeof(*p)+n);if(!p)return NULL;p->h.bytes=n;
    s->malloc_size+=sizeof(*p)+n;++s->malloc_count;a->live+=sizeof(*p)+n;++a->allocations;return p+1;
}
static void release(JSMallocState *s,void *ptr){if(!ptr)return;union block *p=(union block *)ptr-1;size_t n=p->h.bytes+sizeof(*p);struct cb_js_allocator *a=s->opaque;s->malloc_size-=n;--s->malloc_count;a->live-=n;--a->allocations;free(p);}
static void *resize(JSMallocState *s,void *ptr,size_t n){
    if(!ptr)return allocate(s,n);
    if(!n){release(s,ptr);return NULL;}
    union block *p=(union block *)ptr-1;size_t old=p->h.bytes;struct cb_js_allocator *a=s->opaque;
    if(n>SIZE_MAX-sizeof(*p) || (n>old && (s->malloc_size>s->malloc_limit || n-old>s->malloc_limit-s->malloc_size)))return NULL;
    if(a->fail_after && ++a->attempts>=a->fail_after)return NULL;
    p=realloc(p,sizeof(*p)+n);if(!p)return NULL;p->h.bytes=n;s->malloc_size=s->malloc_size-old+n;a->live=a->live-old+n;return p+1;
}
static size_t usable(const void *ptr){return ptr?((const union block *)ptr-1)->h.bytes:0;}
JSRuntime *cb_js_new(struct cb_js_allocator *allocator,size_t memory_limit,size_t stack_limit){
    const JSMallocFunctions functions={allocate,release,resize,usable};JSRuntime *rt=JS_NewRuntime2(&functions,allocator);if(rt){JS_SetMemoryLimit(rt,memory_limit);JS_SetMaxStackSize(rt,stack_limit);}return rt;
}

JSValue cb_js_bytes(JSContext *ctx,const unsigned char *data,size_t bytes){
    JSValue args[3]={JS_NewArrayBufferCopy(ctx,data,bytes),JS_UNDEFINED,JS_UNDEFINED};
    if(JS_IsException(args[0]))return args[0];
    /* The low-level constructor consumes the full constructor argument vector;
     * explicitly supply omitted offset/length, not adjacent C stack storage. */
    JSValue result=JS_NewTypedArray(ctx,3,args,JS_TYPED_ARRAY_UINT8);JS_FreeValue(ctx,args[0]);return result;
}
