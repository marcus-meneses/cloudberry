/* Native editor behavioral and allocation-failure checks; no guest fixture. */
#include "../apps/editor/model.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static long fail_after=-1;
void *__real_malloc(size_t);void *__real_calloc(size_t,size_t);
void *__wrap_malloc(size_t n){if(fail_after==0)return NULL;if(fail_after>0)--fail_after;return __real_malloc(n);}
void *__wrap_calloc(size_t n,size_t s){if(fail_after==0)return NULL;if(fail_after>0)--fail_after;return __real_calloc(n,s);}
static void open_text(struct ed_doc *d,const char *s){assert(ed_open(d,s,strlen(s),""));}
static void content(struct ed_doc *d,const char *s){size_t n;char *text=ed_serialize(d,&n);assert(text&&n==strlen(s)&&!memcmp(text,s,n));free(text);}
static void insert(struct ed_doc *d,const char *s){uint32_t *p;size_t n;assert(ed_decode((const unsigned char *)s,strlen(s),&p,&n));assert(ed_insert(d,p,n));free(p);}
int main(void){
 struct ed_doc d={0};open_text(&d,"a🍓b\nsecond");d.state.col=2;assert(ed_backspace(&d));content(&d,"ab\nsecond");ed_undo(&d);content(&d,"a🍓b\nsecond");assert(d.state.col==2);ed_redo(&d);content(&d,"ab\nsecond");insert(&d,"x\ny");content(&d,"ax\nyb\nsecond");assert(d.state.row==1&&d.state.col==1);ed_move(&d,74);assert(ed_backspace(&d));content(&d,"axyb\nsecond");assert(ed_delete(&d));content(&d,"axb\nsecond");
 open_text(&d,"hello\nworld");ed_move(&d,77);insert(&d,"!");assert(ed_dirty(&d)&&ed_saved(&d,"/a.txt")&&!ed_dirty(&d));ed_undo(&d);assert(ed_dirty(&d));ed_redo(&d);assert(!ed_dirty(&d));d.state.row=1;d.state.col=0;d.overwrite=1;insert(&d,"W");content(&d,"hello!\nWorld");assert(ed_copy(&d)&&ed_cut(&d));content(&d,"hello!");d.overwrite=0;assert(ed_insert(&d,d.clipboard,d.clipboard_length));content(&d,"World\nhello!");uint32_t find[]={'W','o','r','l','d'};assert(ed_find(&d,find,5)&&d.state.row==0&&d.state.col==0);
 const char *roundtrip="\xef\xbb\xbf" "ação 🍓\r\nnext\r\n";open_text(&d,roundtrip);assert(d.bom&&d.crlf&&!ed_dirty(&d));content(&d,roundtrip);unsigned char invalid[][4]={{0xc0,0xaf},{0xed,0xa0,0x80},{0xf4,0x90,0x80,0x80},{0xe2,0x82},{0x80}};size_t sizes[]={2,3,4,2,1};for(size_t i=0;i<5;i++){assert(!ed_open(&d,invalid[i],sizes[i],"/bad"));content(&d,roundtrip);}
 open_text(&d,"one\ntwo");ed_move(&d,77);assert(ed_delete(&d));content(&d,"onetwo");assert(ed_cut(&d));content(&d,"");ed_undo(&d);content(&d,"onetwo");assert(d.state.col==3);ed_redo(&d);assert(ed_backspace(&d)&&ed_delete(&d));content(&d,"");
 open_text(&d,"first\nsecond\nthird");d.state.row=1;d.state.col=3;ed_scroll(&d,100);assert(d.top==2&&d.state.row==1&&d.state.col==3&&!ed_dirty(&d));ed_scroll(&d,-100);assert(d.top==0&&d.state.row==1);insert(&d,"X");content(&d,"first\nsecXond\nthird");
 unsigned char embedded[]={'a',0,'b'};assert(ed_open(&d,embedded,3,"/nul"));size_t n;char *bytes=ed_serialize(&d,&n);assert(bytes&&n==3&&!memcmp(bytes,embedded,n));free(bytes);
 /* Every failed edit/open allocation preserves document, history and caret. */
 for(long budget=0;budget<12;budget++){open_text(&d,"old\ntext");d.state.col=2;struct ed_snapshot *undo=d.undo;fail_after=budget;uint32_t add[]={'X','\n','Y'};int ok=ed_insert(&d,add,3);fail_after=-1;if(!ok){content(&d,"old\ntext");assert(d.undo==undo&&d.state.row==0&&d.state.col==2&&!ed_dirty(&d));}else content(&d,"olX\nYd\ntext");}
 for(long budget=0;budget<10;budget++){open_text(&d,"original");fail_after=budget;int ok=ed_open(&d,"replacement\nfile",16,"/new");fail_after=-1;if(!ok){content(&d,"original");assert(!ed_dirty(&d));}else content(&d,"replacement\nfile");}
 ed_destroy(&d);puts("native editor model: passed (UTF-8, history, viewport, failures)");return 0;
}
