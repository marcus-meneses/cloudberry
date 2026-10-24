#include "bindings.h"
#include <string.h>
#include <stdlib.h>
/* Canonical names are also the engine's per-runtime cache keys. No host paths,
 * bytecode loading, package search, network fetches or policy bypasses. */
static char *normalize(JSContext *ctx,const char *base,const char *name,void *opaque){
    (void)opaque;if(!strcmp(name,"cloudberry"))return js_strdup(ctx,name);
    if(name[0]!='/' && strncmp(name,"./",2) && strncmp(name,"../",3)){JS_ThrowReferenceError(ctx,"module must be absolute or relative: %s",name);return NULL;}
    char directory[4096],path[4096];size_t n=strlen(base);if(n>=sizeof(directory)){JS_ThrowRangeError(ctx,"module path too long");return NULL;}memcpy(directory,base,n+1);char *slash=strrchr(directory,'/');if(slash){if(slash==directory)slash[1]=0;else *slash=0;}else strcpy(directory,cb_libc_cwd());
    if(!cb_path(path,sizeof(path),directory,name)){JS_ThrowRangeError(ctx,"module path too long");return NULL;}return js_strdup(ctx,path);
}
static void import_meta(JSContext *ctx,JSValue value,const char *name){JSModuleDef *m=JS_VALUE_GET_PTR(value);JSValue meta=JS_GetImportMeta(ctx,m);JS_SetPropertyStr(ctx,meta,"url",JS_NewString(ctx,name));JS_FreeValue(ctx,meta);}
static JSModuleDef *load(JSContext *ctx,const char *name,void *opaque){
    struct js_host *h=opaque;unsigned char *data;size_t n;h->os->failure_bytes=0;cb_status_t status=cb_runtime_load(h->os,"/",name,&data,&n);if(status){JS_Throw(ctx,cb_js_error(ctx,status));return NULL;}
    JSValue value=JS_Eval(ctx,(char *)data,n,name,JS_EVAL_TYPE_MODULE|JS_EVAL_FLAG_COMPILE_ONLY);cb_heap_free(&h->os->heap,data);if(JS_IsException(value))return NULL;import_meta(ctx,value,name);JSModuleDef *m=JS_VALUE_GET_PTR(value);JS_FreeValue(ctx,value);return m;
}
static int native_init(JSContext *ctx,JSModuleDef *m){JSValue global=JS_GetGlobalObject(ctx),system=JS_GetPropertyStr(ctx,global,"system");JS_FreeValue(ctx,global);return JS_SetModuleExport(ctx,m,"system",system);}
int cb_js_modules(struct js_host *h){JS_SetModuleLoaderFunc(h->rt,normalize,load,h);JSModuleDef *m=JS_NewCModule(h->ctx,"cloudberry",native_init);return !m || JS_AddModuleExport(h->ctx,m,"system")<0?-1:0;}
JSValue cb_js_eval_file(struct js_host *h,const char *name){
    char path[4096];if(!cb_path(path,sizeof(path),cb_libc_cwd(),name))return JS_ThrowRangeError(h->ctx,"script path too long");unsigned char *data;size_t n;h->os->failure_bytes=0;cb_status_t status=cb_runtime_load(h->os,"/",path,&data,&n);if(status)return JS_Throw(h->ctx,cb_js_error(h->ctx,status));
    JSValue compiled=JS_Eval(h->ctx,(char *)data,n,path,JS_EVAL_TYPE_MODULE|JS_EVAL_FLAG_COMPILE_ONLY);cb_heap_free(&h->os->heap,data);if(JS_IsException(compiled))return compiled;import_meta(h->ctx,compiled,path);return JS_EvalFunction(h->ctx,compiled);
}
