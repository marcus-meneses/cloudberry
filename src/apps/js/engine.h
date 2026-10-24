#ifndef CLOUDBERRY_JS_ENGINE_H
#define CLOUDBERRY_JS_ENGINE_H
#include <quickjs.h>
#include <stddef.h>
struct cb_js_allocator {size_t live,allocations,attempts,fail_after;};
JSValue cb_js_bytes(JSContext *,const unsigned char *,size_t);
JSRuntime *cb_js_new(struct cb_js_allocator *,size_t memory_limit,size_t stack_limit);
#endif
