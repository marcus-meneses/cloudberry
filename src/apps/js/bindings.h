#ifndef CLOUDBERRY_JS_BINDINGS_H
#define CLOUDBERRY_JS_BINDINGS_H
#include "engine.h"
#include "../../sdk/libc.h"
struct js_job;
struct js_host {JSRuntime *rt;JSContext *ctx;struct cb_runtime *os;struct js_job *jobs;JSValue standard[3];unsigned failures;};
JSValue cb_js_error(JSContext *,cb_status_t);
typedef JSValue (*cb_js_decode_fn)(JSContext *,JSValueConst,struct cb_client *,cb_status_t);
typedef void (*cb_js_done_fn)(JSContext *,JSValueConst);
JSValue cb_js_request(JSContext *,JSValueConst,cb_handle_t,struct cb_interface_id,unsigned,const void *,size_t,cb_js_decode_fn,cb_js_done_fn);
void cb_js_cancel(JSContext *,JSValueConst);
int cb_js_devices(JSContext *,JSValue);
int cb_js_bind(struct js_host *,int argc,char **argv);
int cb_js_step(struct js_host *,int block);
int cb_js_drain(struct js_host *);
void cb_js_unbind(struct js_host *);
void cb_js_dump(JSContext *,JSValueConst);
int cb_js_modules(struct js_host *);
JSValue cb_js_eval_file(struct js_host *,const char *);
#endif
