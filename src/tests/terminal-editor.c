#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../apps/terminal/editor.h"
#include "../apps/terminal/parser.h"
static void set(struct term_editor *e,const char *text){term_reset(e);assert(term_replace(e,0,0,text,strlen(text)));}
static void context(struct term_editor *e,const char *text,size_t cursor,const char *prefix,size_t begin,size_t end){set(e,text);e->cursor=cursor;char result[128];size_t a,b;assert(term_completion(e,&a,&b,result,sizeof(result)) && a==begin && b==end && !strcmp(result,prefix));}
int main(void){
    struct {unsigned char before[16];char data[64];unsigned char after[16];} memory;memset(&memory,0xaa,sizeof(memory));struct term_editor e={.data=memory.data,.capacity=sizeof(memory.data)};
    set(&e,"echo ac");e.cursor=6;assert(term_insert(&e,'b') && !strcmp(e.data,"echo abc") && e.cursor==7);term_delete(&e);assert(!strcmp(e.data,"echo ab"));term_backspace(&e);assert(!strcmp(e.data,"echo a"));
    set(&e,"a");assert(term_insert(&e,0xe1));assert(term_insert(&e,0x4e2d));assert(term_insert(&e,0x1f600));assert(e.length==10 && term_codepoint(e.data,e.length,6)==0x1f600);term_backspace(&e);assert(e.length==6);e.cursor=1;e.overwrite=1;assert(term_insert(&e,'b') && !strcmp(e.data,"ab中"));term_delete(&e);assert(!strcmp(e.data,"ab"));e.overwrite=0;
    set(&e,"0123456789");size_t column;assert(term_view(&e,4,&column)==10 && column==3 && e.view==7);e.cursor=0;assert(term_view(&e,4,&column)==4 && column==0 && e.view==0);
    context(&e,"cat /system/ap",14,"/system/ap",4,14);
    context(&e,"cat 'some fo' next",12,"some fo",4,13);
    context(&e,"cat some\\ file tail",14,"some file",4,14);
    context(&e,"echo foo => /sys",16,"/sys",12,16);
    context(&e,"cat /foo",4,"",4,8);
    context(&e,"cat ",4,"",4,4);
    char escaped[128],storage[128];struct term_token tokens[32];size_t count;const char name[]="folder with 'quotes' =>|file";size_t n=term_escape(escaped,sizeof(escaped),name,strlen(name));assert(n!=SIZE_MAX && term_tokens(escaped,storage,tokens,32,&count) && count==1 && !strcmp(tokens[0].text,name));assert(term_escape(escaped,2," x",2)==SIZE_MAX);
    set(&e,"safe");char large[65];memset(large,'z',sizeof(large));assert(!term_replace(&e,0,0,large,sizeof(large)) && !strcmp(e.data,"safe"));assert(!term_replace(&e,5,6,"x",1));
    for(unsigned i=0;i<16;++i)assert(memory.before[i]==0xaa && memory.after[i]==0xaa);
    puts("PASS terminal editor: UTF-8 insertion/overwrite/deletion, cursor viewport, quote-aware path context, shell escaping and bounded buffers");return 0;
}
