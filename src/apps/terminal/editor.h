#ifndef CLOUDBERRY_TERMINAL_EDITOR_H
#define CLOUDBERRY_TERMINAL_EDITOR_H
#include <stddef.h>
#include <stdint.h>
struct term_editor {char *data;size_t capacity,length,cursor,view;unsigned overwrite;};
static inline size_t term_previous(const char *s,size_t at){if(at){--at;while(at && ((unsigned char)s[at]&0xc0)==0x80)--at;}return at;}
static inline size_t term_next(const char *s,size_t length,size_t at){if(at<length){++at;while(at<length && ((unsigned char)s[at]&0xc0)==0x80)++at;}return at;}
static inline void term_reset(struct term_editor *e){e->length=e->cursor=e->view=0;e->data[0]=0;}
static inline int term_replace(struct term_editor *e,size_t from,size_t to,const char *text,size_t bytes){
    if(from>to || to>e->length || bytes>=e->capacity || e->length-(to-from)>=e->capacity-bytes)return 0;
    size_t tail=e->length-to;
    if(from+bytes>to){for(size_t i=tail+1;i;--i)e->data[from+bytes+i-1]=e->data[to+i-1];}
    else for(size_t i=0;i<=tail;++i)e->data[from+bytes+i]=e->data[to+i];
    for(size_t i=0;i<bytes;++i)e->data[from+i]=text[i];
    e->length=e->length-(to-from)+bytes;e->cursor=from+bytes;if(e->view>e->cursor)e->view=e->cursor;return 1;
}
static inline int term_insert(struct term_editor *e,uint32_t cp){char b[4];size_t n=0;if(cp<32 || cp==127 || cp>0x10ffff || (cp>=0xd800 && cp<=0xdfff))return 0;
    if(cp<128)b[n++]=cp;else if(cp<2048){b[n++]=0xc0|(cp>>6);b[n++]=0x80|(cp&63);}else if(cp<65536){b[n++]=0xe0|(cp>>12);b[n++]=0x80|((cp>>6)&63);b[n++]=0x80|(cp&63);}else {b[n++]=0xf0|(cp>>18);b[n++]=0x80|((cp>>12)&63);b[n++]=0x80|((cp>>6)&63);b[n++]=0x80|(cp&63);}return term_replace(e,e->cursor,e->overwrite?term_next(e->data,e->length,e->cursor):e->cursor,b,n);
}
static inline void term_backspace(struct term_editor *e){if(e->cursor)(void)term_replace(e,term_previous(e->data,e->cursor),e->cursor,"",0);}
static inline void term_delete(struct term_editor *e){if(e->cursor<e->length)(void)term_replace(e,e->cursor,term_next(e->data,e->length,e->cursor),"",0);}
static inline uint32_t term_codepoint(const char *s,size_t bytes,size_t at){if(at>=bytes)return ' ';unsigned c=(unsigned char)s[at++];if(c<128)return c;unsigned n=c<0xe0?1:c<0xf0?2:3;uint32_t cp=c&((1u<<(6-n))-1);while(n--){if(at>=bytes || ((unsigned char)s[at]&0xc0)!=0x80)return 0xfffd;cp=(cp<<6)|((unsigned char)s[at++]&63);}return cp;}
/* Keep the insertion position visible in a single-row horizontal viewport. */
static inline size_t term_view(struct term_editor *e,size_t columns,size_t *cursor){
    if(e->view>e->cursor)e->view=e->cursor;
    size_t cells=0;for(size_t i=e->view;i<e->cursor;i=term_next(e->data,e->length,i))++cells;
    while(cells>=columns && e->view<e->cursor){e->view=term_next(e->data,e->length,e->view);--cells;}
    *cursor=cells;size_t end=e->view;for(size_t i=0;i<columns && end<e->length;++i)end=term_next(e->data,e->length,end);return end;
}
/* Decode the token at the cursor with the same quote/backslash rules as the
 * command parser. Full raw bounds allow replacement without damaging later args. */
static inline int term_completion(const struct term_editor *e,size_t *start,size_t *end,char *prefix,size_t capacity){
    size_t i=0;*start=*end=e->cursor;prefix[0]=0;
    while(i<e->length){while(i<e->length && (e->data[i]==' ' || e->data[i]=='\t'))++i;
        if(i>e->cursor){*start=*end=e->cursor;return 1;}
        if((e->data[i]=='=' || e->data[i]=='-' || e->data[i]=='|') && e->data[i+1]=='>'){i+=2;continue;}
        size_t begin=i,w=0;char quote=0;
        while(i<e->length){char c=e->data[i];if(!quote && (c==' ' || c=='\t' || ((c=='=' || c=='-' || c=='|') && e->data[i+1]=='>')))break;
            if(c=='\'' || c=='"'){if(!quote){quote=c;++i;continue;}if(quote==c){quote=0;++i;continue;}}
            if(c=='\\' && quote!='\''){if(++i==e->length)break;c=e->data[i];}
            if(i<e->cursor){if(w+1>=capacity)return 0;prefix[w++]=c;}++i;
        }
        if(e->cursor>=begin && e->cursor<=i){prefix[w]=0;*start=begin;*end=i;return 1;}
    }return 1;
}
/* Encode one literal argument outside quotes, including shell operators. */
static inline size_t term_escape(char *out,size_t capacity,const char *text,size_t bytes){size_t n=0;for(size_t i=0;i<bytes;++i){char c=text[i];int escape=c==' ' || c=='\t' || c=='\\' || c=='\'' || c=='"' || c=='=' || c=='|' || c=='>';if(n+(size_t)escape+1>=capacity)return SIZE_MAX;if(escape)out[n++]='\\';out[n++]=c;}out[n]=0;return n;}
#endif
