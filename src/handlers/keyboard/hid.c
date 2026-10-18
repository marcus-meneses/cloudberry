#include "hid.h"
struct parser {const unsigned char *at,*end;};
static void whitespace(struct parser *p){while(p->at<p->end && (*p->at==' '||*p->at=='\t'||*p->at=='\r'||*p->at=='\n'))++p->at;}
static int take(struct parser *p,char c){whitespace(p);if(p->at==p->end || *p->at!=c)return 0;++p->at;return 1;}
static int word(struct parser *p,const char *s){whitespace(p);const unsigned char *at=p->at;while(*s){if(at==p->end || *at++!=*s++)return 0;}p->at=at;return 1;}
static int number(struct parser *p,uint32_t *out){whitespace(p);if(p->at==p->end || *p->at<'0'||*p->at>'9')return 0;uint32_t n=0;int zero=*p->at=='0';unsigned digits=0;while(p->at<p->end && *p->at>='0'&&*p->at<='9'){if(n>(UINT32_MAX-9)/10 || (zero && digits))return 0;n=n*10+*p->at++-'0';++digits;}*out=n;return 1;}
int keyboard_layout_parse(struct keyboard_layout *layout,const void *input,size_t bytes){
    if(!layout || !input || bytes>UINTPTR_MAX-(uintptr_t)input)return 0;
    unsigned char *zero=(void *)layout;for(size_t i=0;i<sizeof(*layout);++i)zero[i]=0;
    struct parser p={input,(const unsigned char *)input+bytes};if(!take(&p,'{'))return 0;unsigned seen=0;uint8_t defined[256]={0};
    for(;;){unsigned bit=0;if(word(&p,"\"version\""))bit=1;else if(word(&p,"\"keys\""))bit=2;else if(word(&p,"\"caps\""))bit=4;else if(word(&p,"\"dead\""))bit=8;else return 0;if(seen&bit || !take(&p,':'))return 0;seen|=bit;
        if(bit==1){uint32_t version;if(!number(&p,&version) || version!=1)return 0;}
        else{if(!take(&p,'['))return 0;whitespace(&p);if(p.at<p.end && *p.at!=']')for(;;){
                if(bit==2){uint32_t usage;if(!take(&p,'[')||!number(&p,&usage)||usage>255||defined[usage])return 0;defined[usage]=1;
                    for(unsigned col=0;col<4;++col){if(!take(&p,','))return 0;uint32_t scalar;if(word(&p,"null"))continue;if(!number(&p,&scalar)||!cb_unicode_scalar(scalar))return 0;layout->keys[usage].codepoint[col]=scalar;layout->keys[usage].present|=1u<<col;}if(!take(&p,']'))return 0;
                }else if(bit==4){uint32_t usage;if(!number(&p,&usage)||usage>255||layout->keys[usage].caps)return 0;layout->keys[usage].caps=1;}
                else{uint32_t usage,col;if(!take(&p,'[')||!number(&p,&usage)||usage>255||!take(&p,',')||!number(&p,&col)||col>3||!take(&p,']')||(layout->keys[usage].dead&(1u<<col)))return 0;layout->keys[usage].dead|=1u<<col;}
                whitespace(&p);if(p.at<p.end && *p.at==']')break;if(!take(&p,','))return 0;
            }if(!take(&p,']'))return 0;
        }whitespace(&p);if(p.at<p.end && *p.at=='}'){++p.at;break;}if(!take(&p,','))return 0;
    }
    for(unsigned i=0;i<256;++i)if((layout->keys[i].dead&~layout->keys[i].present) || (layout->keys[i].caps && (layout->keys[i].present&3)!=3))return 0;
    whitespace(&p);return (seen&3)==3 && p.at==p.end;
}
static uint32_t spacing(uint32_t c){return c==0x301?0xb4:c==0x300?'`':c==0x308?0xa8:c==0x303?'~':c==0x302?'^':c;}
static void text(struct keyboard_hid *s,const struct keyboard_layout *layout,unsigned usage,uint64_t origin,uint32_t flags,keyboard_emit emit,void *context){
    const struct keyboard_key *key=&layout->keys[usage];unsigned col=!!(s->modifiers&0x22);if(s->modifiers&0x40)col+=2;if(col<2 && key->caps && s->caps)col^=1;if(!(key->present&(1u<<col)))return;uint32_t scalar=key->codepoint[col];
    if(key->dead&(1u<<col)){if(s->accent)emit(context,CB_KEY_TEXT,usage,spacing(s->accent),s->modifiers,origin,flags);s->accent=scalar;return;}
    if(s->modifiers&0x11){if(scalar>='a'&&scalar<='z')scalar-=96;else if(scalar>='A'&&scalar<='Z')scalar-=64;else if(scalar==' ')scalar=0;}
    if(s->accent){if(scalar==' '){scalar=spacing(s->accent);s->accent=0;}else if(scalar==8){s->accent=0;return;}else{emit(context,CB_KEY_TEXT,usage,scalar,s->modifiers,origin,flags);scalar=s->accent;s->accent=0;}}
    emit(context,CB_KEY_TEXT,usage,scalar,s->modifiers,origin,flags);
}
void keyboard_reset(struct keyboard_hid *s,keyboard_emit emit,void *context){uint8_t caps=s->caps;unsigned char *p=(void *)s;for(size_t i=0;i<sizeof(*s);++i)p[i]=0;s->caps=caps;emit(context,CB_KEY_RESET,0,0,0,0,0);}
int keyboard_report(struct keyboard_hid *s,const struct keyboard_layout *layout,const uint8_t report[8],keyboard_emit emit,void *context){
    if(report[1]){keyboard_reset(s,emit,context);return 0;}uint8_t next[256]={0};for(unsigned i=0;i<8;++i)next[224+i]=!!(report[0]&(1u<<i));
    for(unsigned i=2;i<8;++i){if((report[i]>0 && report[i]<4) || report[i]>=224){keyboard_reset(s,emit,context);return 0;}if(report[i])next[report[i]]=1;}
    s->modifiers=report[0];for(unsigned order=0;order<2;++order)for(unsigned direction=0;direction<2;++direction)for(unsigned i=order?4:224;i<(order?224:232);++i){
        if(!direction && s->pressed[i] && !next[i]){s->pressed[i]=0;emit(context,CB_KEY_UP,i,0,s->modifiers,0,0);if(s->repeat_key==i)s->repeat_key=0;}
        else if(direction && !s->pressed[i] && next[i]){s->pressed[i]=1;uint64_t origin=emit(context,CB_KEY_DOWN,i,0,s->modifiers,0,0);s->origin[i]=origin;if(i==57)s->caps^=1;if(i<224){if(i!=57 && i!=71 && i!=83)s->repeat_key=i;text(s,layout,i,origin,0,emit,context);}}
    }
    if(!s->repeat_key){uint64_t newest=0;for(unsigned i=4;i<224;++i)if(s->pressed[i] && i!=57 && i!=71 && i!=83 && s->origin[i]>newest){newest=s->origin[i];s->repeat_key=i;}}
    return 1;
}
void keyboard_repeat(struct keyboard_hid *s,const struct keyboard_layout *layout,keyboard_emit emit,void *context){unsigned usage=s->repeat_key;if(!usage || !s->pressed[usage])return;uint64_t origin=emit(context,CB_KEY_REPEAT,usage,0,s->modifiers,s->origin[usage],CB_KEY_SYNTHETIC);text(s,layout,usage,origin,CB_KEY_SYNTHETIC,emit,context);}

/* Handler-owned configuration, independent of bootstrap/provider definitions. */
int keyboard_config_layout(const void *data,size_t bytes,const char **name,size_t *name_bytes){
    const char *p=data;size_t at=0;int seen=0;
    for(size_t i=0;i<bytes;++i)if(!p[i] || ((unsigned char)p[i]<32 && p[i]!='\r' && p[i]!='\n' && p[i]!='\t') || (unsigned char)p[i]>=127)return 0;
    while(at<bytes){size_t first=at;while(at<bytes && p[at]!='\n')++at;size_t end=at;if(at<bytes)++at;while(first<end && (p[first]==' ' || p[first]=='\t'))++first;while(end>first && (p[end-1]==' ' || p[end-1]=='\t' || p[end-1]=='\r'))--end;
        if(first==end || p[first]=='#')continue;
        static const char key[]="layout=";if(seen || end-first<=sizeof(key)-1)return 0;for(unsigned i=0;i<sizeof(key)-1;++i)if(p[first+i]!=key[i])return 0;first+=sizeof(key)-1;
        for(size_t i=first;i<end;++i)if(!((p[i]>='a'&&p[i]<='z') || (p[i]>='A'&&p[i]<='Z') || (p[i]>='0'&&p[i]<='9') || p[i]=='_' || p[i]=='-'))return 0;
        *name=p+first;*name_bytes=end-first;seen=1;
    }return seen;
}
