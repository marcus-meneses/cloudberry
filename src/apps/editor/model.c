#include "model.h"
#include <stdlib.h>
#include <string.h>
#include <limits.h>
static char *ed_duplicate(const char *s){
    size_t n=strlen(s)+1;
    char *copy=malloc(n);
    if(copy)memcpy(copy,s,n);
    return copy;
}
static size_t min_size(size_t a,size_t b){
    return a<b?a:b;
}
static struct ed_line *line_new(const uint32_t *text,size_t n){
    if(n>(SIZE_MAX-sizeof(struct ed_line))/4)return NULL;
    struct ed_line *l=malloc(sizeof(*l)+n*4);
    if(l){
        l->refs=1;
        l->length=n;
        if(n)memcpy(l->text,text,n*4);
    }
    return l;
}
static void line_drop(struct ed_line *l){
    if(l && !--l->refs)free(l);
}
static void state_drop(struct ed_state *s){
    for(size_t i=0;i<s->count;i++)line_drop(s->lines[i]);
    free(s->lines);
    memset(s,0,sizeof(*s));
}
static int state_clone(struct ed_state *dst,const struct ed_state *s){
    if(s->count>SIZE_MAX/sizeof(*s->lines))return 0;
    *dst=*s;
    dst->lines=malloc(s->count*sizeof(*s->lines));
    if(!dst->lines){
        memset(dst,0,sizeof(*dst));
        return 0;
    }
    for(size_t i=0;i<s->count;i++){
        dst->lines[i]=s->lines[i];
        ++dst->lines[i]->refs;
    }
    return 1;
}
static void history_drop(struct ed_snapshot *p){
    while(p){
        struct ed_snapshot *next=p->next;
        state_drop(&p->state);
        free(p);
        p=next;
    }
}
static int state_equal(const struct ed_state *a,const struct ed_state *b){
    if(a->count!=b->count)return 0;
    for(size_t i=0;i<a->count;i++){
        const struct ed_line *x=a->lines[i],*y=b->lines[i];
        if(x!=y && (x->length!=y->length || memcmp(x->text,y->text,x->length*4)))return 0;
    }
    return 1;
}
int ed_decode(const unsigned char *p,size_t n,uint32_t **out,size_t *length){
    *out=NULL;
    *length=0;
    if(n>SIZE_MAX/4)return 0;
    uint32_t *text=malloc((n?n:1)*4);
    if(!text)return 0;
    size_t used=0;
    for(size_t i=0;i<n;){
        unsigned c=p[i++],extra=0;
        uint32_t value=c,minimum=0;
        if(c>=0xc2&&c<=0xdf){
            extra=1;
            value=c&31;
            minimum=0x80;
        }
        else if(c>=0xe0&&c<=0xef){
            extra=2;
            value=c&15;
            minimum=0x800;
        }
        else if(c>=0xf0&&c<=0xf4){
            extra=3;
            value=c&7;
            minimum=0x10000;
        }
        else if(c>=0x80)goto fail;
        for(unsigned j=0;j<extra;j++){
            if(i==n||(p[i]&0xc0)!=0x80)goto fail;
            value=(value<<6)|(p[i++]&63);
        }
        if(value<minimum||value>0x10ffff||(value>=0xd800&&value<=0xdfff))goto fail;
        text[used++]=value;
    }
    *out=text;
    *length=used;
    return 1;
    fail:free(text);
    return 0;
}
char *ed_utf8(const uint32_t *p,size_t n,size_t *bytes){
    if(n>(SIZE_MAX-1)/4)return NULL;
    char *s=malloc(n*4+1);
    if(!s)return NULL;
    size_t at=0;
    for(size_t i=0;i<n;i++){
        uint32_t c=p[i];
        if(c<0x80)s[at++]=c;
        else if(c<0x800){
            s[at++]=0xc0|(c>>6);
            s[at++]=0x80|(c&63);
        }
        else if(c<0x10000){
            s[at++]=0xe0|(c>>12);
            s[at++]=0x80|((c>>6)&63);
            s[at++]=0x80|(c&63);
        }
        else{
            s[at++]=0xf0|(c>>18);
            s[at++]=0x80|((c>>12)&63);
            s[at++]=0x80|((c>>6)&63);
            s[at++]=0x80|(c&63);
        }
    }
    s[at]=0;
    if(bytes)*bytes=at;
    return s;
}
void ed_destroy(struct ed_doc *d){
    state_drop(&d->state);
    state_drop(&d->saved);
    history_drop(d->undo);
    history_drop(d->redo);
    free(d->path);
    free(d->clipboard);
    memset(d,0,sizeof(*d));
}
int ed_open(struct ed_doc *d,const void *data,size_t n,const char *path){
    uint32_t *text;
    size_t length;
    if(!ed_decode(data,n,&text,&length))return 0;
    struct ed_doc fresh={
        .overwrite=d->overwrite
    };
    fresh.bom=length&&text[0]==0xfeff;
    size_t used=0,count=1;
    for(size_t i=fresh.bom?1:0;i<length;i++){
        if(text[i]=='\r'&&i+1<length&&text[i+1]=='\n'){
            fresh.crlf=1;
            continue;
        }
        if(text[i]=='\n')++count;
        text[used++]=text[i];
    }
    if(count>SIZE_MAX/sizeof(struct ed_line *))goto fail;
    fresh.state.lines=calloc(count,sizeof(*fresh.state.lines));
    fresh.path=ed_duplicate(path?path:"");
    if(!fresh.state.lines||!fresh.path)goto fail;
    size_t begin=0;
    for(size_t i=0;i<=used;i++)if(i==used||text[i]=='\n'){
        struct ed_line *l=line_new(text+begin,i-begin);
        if(!l)goto fail;
        fresh.state.lines[fresh.state.count++]=l;
        begin=i+1;
    }
    if(!state_clone(&fresh.saved,&fresh.state))goto fail;
    free(text);
    fresh.clipboard=d->clipboard;
    fresh.clipboard_length=d->clipboard_length;
    d->clipboard=NULL;
    ed_destroy(d);
    *d=fresh;
    return 1;
    fail:free(text);
    ed_destroy(&fresh);
    return 0;
}
int ed_dirty(const struct ed_doc *d){
    return !state_equal(&d->state,&d->saved);
}
int ed_saved(struct ed_doc *d,const char *path){
    struct ed_state saved={
        0
    };
    char *name=ed_duplicate(path);
    if(!name||!state_clone(&saved,&d->state)){
        free(name);
        return 0;
    }
    state_drop(&d->saved);
    d->saved=saved;
    free(d->path);
    d->path=name;
    return 1;
}
char *ed_serialize(const struct ed_doc *d,size_t *bytes){
    size_t count=d->bom?1:0;
    for(size_t i=0;i<d->state.count;i++){
        size_t n=d->state.lines[i]->length;
        if(n>SIZE_MAX-count)return NULL;
        count+=n;
        if(i+1<d->state.count){
            if(count>SIZE_MAX-2)return NULL;
            count+=d->crlf?2:1;
        }
    }
    if(count>SIZE_MAX/4)return NULL;
    uint32_t *text=malloc((count?count:1)*4);
    if(!text)return NULL;
    size_t at=0;
    if(d->bom)text[at++]=0xfeff;
    for(size_t i=0;i<d->state.count;i++){
        struct ed_line *l=d->state.lines[i];
        memcpy(text+at,l->text,l->length*4);
        at+=l->length;
        if(i+1<d->state.count){
            if(d->crlf)text[at++]='\r';
            text[at++]='\n';
        }
    }
    char *s=ed_utf8(text,at,bytes);
    free(text);
    return s;
}
static int commit(struct ed_doc *d,struct ed_state *next){
    if(state_equal(&d->state,next)){
        state_drop(next);
        return 1;
    }
    struct ed_snapshot *old=malloc(sizeof(*old));
    if(!old){
        state_drop(next);
        return 0;
    }
    *old=(struct ed_snapshot){
        d->undo,d->state
    };
    d->undo=old;
    d->state=*next;
    history_drop(d->redo);
    d->redo=NULL;
    return 1;
}
static int replace(struct ed_doc *d,size_t last_row,size_t last_col,const uint32_t *input,size_t n){
    struct ed_state *s=&d->state;
    size_t first_row=s->row,first_col=s->col,parts=1;
    for(size_t i=0;i<n;i++)if(input[i]=='\n')parts++;
    size_t count=s->count-(last_row-first_row)+parts-1;
    if(count>SIZE_MAX/sizeof(struct ed_line *))return 0;
    struct ed_state next={
        .lines=calloc(count,sizeof(*next.lines)),.count=count,.row=first_row+parts-1
    };
    if(!next.lines)return 0;
    for(size_t i=0;i<first_row;i++){
        next.lines[i]=s->lines[i];
        ++next.lines[i]->refs;
    }
    size_t begin=0,index=first_row;
    for(size_t i=0;i<=n;i++)if(i==n||input[i]=='\n'){
        size_t prefix=index==first_row?first_col:0,tail=i==n?s->lines[last_row]->length-last_col:0;
        if(i-begin>SIZE_MAX-prefix || tail>SIZE_MAX-prefix-(i-begin))goto fail;
        size_t total=prefix+i-begin+tail;
        if(total>SIZE_MAX/4)goto fail;
        uint32_t *joined=malloc((total?total:1)*4);
        if(!joined)goto fail;
        if(prefix)memcpy(joined,s->lines[first_row]->text,prefix*4);
        if(i>begin)memcpy(joined+prefix,input+begin,(i-begin)*4);
        if(tail)memcpy(joined+prefix+i-begin,s->lines[last_row]->text+last_col,tail*4);
        next.lines[index]=line_new(joined,total);
        free(joined);
        if(!next.lines[index])goto fail;
        if(i==n)next.col=prefix+i-begin;
        ++index;
        begin=i+1;
    }
    for(size_t i=last_row+1;i<s->count;i++){
        next.lines[index++]=s->lines[i];
        ++s->lines[i]->refs;
    }
    return commit(d,&next);
    fail:state_drop(&next);
    return 0;
}
int ed_insert(struct ed_doc *d,const uint32_t *p,size_t n){
    size_t end=d->state.col;
    if(d->overwrite){
        int multiline=0;
        for(size_t i=0;i<n;i++)if(p[i]=='\n')multiline=1;
        if(!multiline)end+=min_size(n,d->state.lines[d->state.row]->length-end);
    }
    return replace(d,d->state.row,end,p,n);
}
int ed_backspace(struct ed_doc *d){
    if(d->state.col){
        size_t old=d->state.col;
        --d->state.col;
        int ok=replace(d,d->state.row,old,NULL,0);
        if(!ok)d->state.col=old;
        else if(d->undo)d->undo->state.col=old;
        return ok;
    }
    if(!d->state.row)return 1;
    size_t row=d->state.row;
    d->state.row--;
    d->state.col=d->state.lines[d->state.row]->length;
    int ok=replace(d,row,0,NULL,0);
    if(!ok){
        d->state.row=row;
        d->state.col=0;
    }
    else if(d->undo){
        d->undo->state.row=row;
        d->undo->state.col=0;
    }
    return ok;
}
int ed_delete(struct ed_doc *d){
    size_t row=d->state.row,col=d->state.col;
    if(col<d->state.lines[row]->length)return replace(d,row,col+1,NULL,0);
    if(row+1<d->state.count)return replace(d,row+1,0,NULL,0);
    return 1;
}
int ed_copy(struct ed_doc *d){
    struct ed_line *l=d->state.lines[d->state.row];
    if(l->length>=SIZE_MAX/4)return 0;
    uint32_t *s=malloc((l->length+1)*4);
    if(!s)return 0;
    memcpy(s,l->text,l->length*4);
    s[l->length]='\n';
    free(d->clipboard);
    d->clipboard=s;
    d->clipboard_length=l->length+1;
    return 1;
}
int ed_cut(struct ed_doc *d){
    if(!ed_copy(d))return 0;
    if(d->state.count==1){
        size_t col=d->state.col;
        d->state.col=0;
        int ok=replace(d,0,d->state.lines[0]->length,NULL,0);
        if(!ok)d->state.col=col;
        else if(d->undo)d->undo->state.col=col;
        return ok;
    }
    struct ed_state next={
        .count=d->state.count-1
    };
    next.lines=malloc(next.count*sizeof(*next.lines));
    if(!next.lines)return 0;
    for(size_t i=0,j=0;i<d->state.count;i++)if(i!=d->state.row){
        next.lines[j++]=d->state.lines[i];
        ++d->state.lines[i]->refs;
    }
    next.row=min_size(d->state.row,next.count-1);
    return commit(d,&next);
}
static void history_move(struct ed_doc *d,struct ed_snapshot **from,struct ed_snapshot **to){
    if(!*from)return;
    struct ed_snapshot *p=*from;
    *from=p->next;
    struct ed_state old=d->state;
    d->state=p->state;
    p->state=old;
    p->next=*to;
    *to=p;
}
void ed_undo(struct ed_doc *d){
    history_move(d,&d->undo,&d->redo);
}
void ed_redo(struct ed_doc *d){
    history_move(d,&d->redo,&d->undo);
}
void ed_move(struct ed_doc *d,unsigned key){
    struct ed_state *s=&d->state;
    if(key==80){
        if(s->col)--s->col;
        else if(s->row){
            --s->row;
            s->col=s->lines[s->row]->length;
        }
    }
    else if(key==79){
        if(s->col<s->lines[s->row]->length)++s->col;
        else if(s->row+1<s->count){
            ++s->row;
            s->col=0;
        }
    }
    else if(key==74)s->col=0;
    else if(key==77)s->col=s->lines[s->row]->length;
    else {
        if(key==82&&s->row)--s->row;
        if(key==81&&s->row+1<s->count)++s->row;
        s->col=min_size(s->col,s->lines[s->row]->length);
    }
}
void ed_scroll(struct ed_doc *d,ptrdiff_t rows){
    if(rows<0){
        size_t n=(size_t)(-(rows+1))+1;
        d->top=n>d->top?0:d->top-n;
    }
    else {
        size_t n=(size_t)rows;
        d->top=n>d->state.count-1-d->top?d->state.count-1:d->top+n;
    }
}
int ed_find(struct ed_doc *d,const uint32_t *needle,size_t n){
    if(!n)return 0;
    for(size_t step=0;step<=d->state.count;step++){
        size_t row=(d->state.row+step)%d->state.count;
        struct ed_line *l=d->state.lines[row];
        size_t start=step?0:d->state.col+1;
        for(size_t col=start;col<=l->length && n<=l->length-col;col++)if(!memcmp(l->text+col,needle,n*4)){
            d->state.row=row;
            d->state.col=col;
            return 1;
        }
    }
    return 0;
}
