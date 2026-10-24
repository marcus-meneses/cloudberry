#include "../../apps/js/engine.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){printf("FAIL JS engine line %d\n",__LINE__);return 1;}}while(0)
static int eval(JSContext *ctx,const char *source){JSValue value=JS_Eval(ctx,source,strlen(source),"engine-test.js",JS_EVAL_TYPE_GLOBAL);int ok=!JS_IsException(value) && JS_ToBool(ctx,value)==1;if(JS_IsException(value)){JSValue e=JS_GetException(ctx);const char *s=JS_ToCString(ctx,e);printf("engine exception: %s\n",s?s:"<OOM>");JS_FreeCString(ctx,s);JS_FreeValue(ctx,e);}JS_FreeValue(ctx,value);return ok;}
int main(void){
    struct cb_js_allocator a={0};JSRuntime *rt=cb_js_new(&a,16*1024*1024,128*1024);CHECK(rt);JSContext *ctx=JS_NewContext(rt);CHECK(ctx);
    unsigned char binary[]={0,77,128,255};JSValue bytes=cb_js_bytes(ctx,binary,sizeof(binary));CHECK(!JS_IsException(bytes));JSValue global=JS_GetGlobalObject(ctx);CHECK(JS_SetPropertyStr(ctx,global,"binary",bytes)==1);JS_FreeValue(ctx,global);
    CHECK(eval(ctx,"binary instanceof Uint8Array && binary.length===4 && binary[0]===0 && binary[1]===77 && binary[2]===128 && binary[3]===255"));
    CHECK(eval(ctx,"let value=0;try{throw new Error('caught')}catch(e){value=e.message==='caught'};value"));
    CHECK(eval(ctx,"'ação 🍓'.normalize('NFC')==='ação 🍓' && /\\p{Letter}+/u.test('ação') && String.fromCodePoint(0x1f353).length===2"));
    CHECK(eval(ctx,"Math.sqrt(4)===2 && Math.abs(Math.sin(.5)-.479425538604203)<1e-15 && Object.is(-0,-0) && 2n**64n===18446744073709551616n"));
    CHECK(eval(ctx,"let overflow=false;try{(function recurse(){return recurse()})()}catch(e){overflow=e instanceof InternalError};overflow"));
    CHECK(eval(ctx,"globalThis.jobValue=0;Promise.resolve(41).then(x=>jobValue=x+1);true"));JSContext *job;while(JS_IsJobPending(rt))CHECK(JS_ExecutePendingJob(rt,&job)>0);CHECK(eval(ctx,"jobValue===42"));
    JS_SetMemoryLimit(rt,a.live+32768);CHECK(eval(ctx,"let oom=false;try{new ArrayBuffer(1024*1024)}catch(e){oom=true};oom"));
    JS_FreeContext(ctx);JS_FreeRuntime(rt);CHECK(!a.live && !a.allocations);
    /* Fail initialization at diverse allocation points, then verify whole-runtime
     * teardown after failures during evaluation, not just arithmetic overflow. */
    for(size_t point=1;point<80;point+=3){a=(struct cb_js_allocator){.fail_after=point};rt=cb_js_new(&a,16*1024*1024,128*1024);if(rt){ctx=JS_NewContext(rt);if(ctx)JS_FreeContext(ctx);JS_FreeRuntime(rt);}CHECK(!a.live && !a.allocations);}
    a=(struct cb_js_allocator){0};rt=cb_js_new(&a,16*1024*1024,128*1024);CHECK(rt);ctx=JS_NewContext(rt);CHECK(ctx);a.fail_after=1;
    JSValue failed=JS_Eval(ctx,"Array.from({length:10000},(_,i)=>({i}))",strlen("Array.from({length:10000},(_,i)=>({i}))"),"oom.js",0);JS_FreeValue(ctx,failed);JSValue error=JS_GetException(ctx);JS_FreeValue(ctx,error);a.fail_after=0;JS_FreeContext(ctx);JS_FreeRuntime(rt);CHECK(!a.live && !a.allocations);
    puts("PASS JS engine: allocator stack exceptions Unicode math jobs OOM cleanup");return 0;
}
