#include "bindings.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
struct rejection {struct rejection *next;JSValue promise,reason;};
static struct rejection *rejections;
static void rejection(JSContext *ctx,JSValueConst promise,JSValueConst reason,JS_BOOL handled,void *opaque){
    struct js_host *h=opaque;struct rejection **at=&rejections;while(*at && JS_VALUE_GET_PTR((*at)->promise)!=JS_VALUE_GET_PTR(promise))at=&(*at)->next;
    if(handled){if(*at){struct rejection *r=*at;*at=r->next;JS_FreeValue(ctx,r->promise);JS_FreeValue(ctx,r->reason);free(r);}return;}
    if(*at)return;
    struct rejection *r=malloc(sizeof(*r));if(!r){++h->failures;return;}*r=(struct rejection){.next=rejections,.promise=JS_DupValue(ctx,promise),.reason=JS_DupValue(ctx,reason)};rejections=r;
}
static int report_rejections(struct js_host *h){int failed=0;while(rejections){struct rejection *r=rejections;rejections=r->next;cb_js_dump(h->ctx,r->reason);JS_FreeValue(h->ctx,r->promise);JS_FreeValue(h->ctx,r->reason);free(r);failed=1;}return failed;}
static int evaluate(struct js_host *h,JSValue value,int display){
    int failed=0;
    if(JS_IsException(value)){
        JSValue error=JS_GetException(h->ctx);cb_js_dump(h->ctx,error);JS_FreeValue(h->ctx,error);failed=1;
    }
    if(cb_js_drain(h)<0)failed=1;
    if(!failed && JS_PromiseState(h->ctx,value)==JS_PROMISE_PENDING){fputs("js: unresolved top-level Promise has no completion source\n",stderr);failed=1;}
    if(!failed && display && !JS_IsUndefined(value)){
        const char *text=JS_ToCString(h->ctx,value);if(text){puts(text);JS_FreeCString(h->ctx,text);}else {JSValue error=JS_GetException(h->ctx);cb_js_dump(h->ctx,error);JS_FreeValue(h->ctx,error);failed=1;}
    }
    JS_FreeValue(h->ctx,value);if(report_rejections(h))failed=1;fflush(stdout);return failed;
}
static int repl(struct js_host *h){
    /* Do not prefetch bytes that a launched child or a JS stdin stream owns. */
    if(setvbuf(stdin,NULL,_IONBF,0))return 1;
    char *line=NULL;size_t used=0,capacity=0;int result=0;
    for(;;){fputs("js > ",stdout);fflush(stdout);used=0;int c;
        while((c=fgetc(stdin))!=EOF && c!='\n'){if(c=='\r')continue;if(used+1>=capacity){size_t next=capacity?capacity*2:256;if(next<=capacity){free(line);return 1;}char *p=realloc(line,next);if(!p){free(line);return 1;}line=p;capacity=next;}line[used++]=(char)c;}
        if(c==EOF && !used)break;
        if(!capacity)continue;
        line[used]=0;if(!strcmp(line,".exit"))break;if(!used)continue;
        result|=evaluate(h,JS_Eval(h->ctx,line,used,"<stdin>",JS_EVAL_TYPE_GLOBAL),1);
    }
    free(line);return result;
}
int main(int argc,char **argv){
    if(argc>1 && (!strcmp(argv[1],"--help") || !strcmp(argv[1],"-h"))){puts("js [script.js [args...]] | js -e source | js -i\nES modules; system handle bindings; .exit leaves interactive mode.");return 0;}
    int expression=argc>1 && !strcmp(argv[1],"-e"),interactive=argc==1 || (argc==2 && !strcmp(argv[1],"-i"));if(expression && argc!=3){fputs("js -e requires one source argument\n",stderr);return 2;}
    struct cb_js_allocator allocator={0};struct js_host h={0};h.rt=cb_js_new(&allocator,128*1024*1024,128*1024);if(!h.rt)return 1;h.ctx=JS_NewContext(h.rt);if(!h.ctx){JS_FreeRuntime(h.rt);return 1;}
    JS_SetHostPromiseRejectionTracker(h.rt,rejection,&h);int start=interactive?argc:expression?argc:1;int result=0;
    if(cb_js_bind(&h,argc-start,argv+start)<0 || cb_js_modules(&h)<0)result=1;
    else if(interactive)result=repl(&h);
    else if(expression)result=evaluate(&h,JS_Eval(h.ctx,argv[2],strlen(argv[2]),"<eval>",JS_EVAL_TYPE_MODULE),0);
    else result=evaluate(&h,cb_js_eval_file(&h,argv[1]),0);
    cb_js_unbind(&h);report_rejections(&h);JS_FreeContext(h.ctx);JS_FreeRuntime(h.rt);if(allocator.live || allocator.allocations){fputs("js: engine allocation leak\n",stderr);result=1;}return result;
}
