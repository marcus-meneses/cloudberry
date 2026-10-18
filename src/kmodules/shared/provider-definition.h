#ifndef CLOUDBERRY_PROVIDER_DEFINITION_H
#define CLOUDBERRY_PROVIDER_DEFINITION_H
#include <stddef.h>
#include <stdint.h>
struct provider_definition {char *executable,*manifest,*ready,*files;uint64_t stack_bytes;uint32_t cpu;unsigned char identity[16];};
static inline int pd_same(const char *a,const char *b){while(*a && *a==*b){++a;++b;}return *a==*b;}
static inline int pd_path(const char *s,int basename){
    if(!s || !*s || *s=='/')return 0;
    const char *part=s;for(const char *p=s;;++p){char c=*p;if(!c || c=='/'){size_t n=p-part;if(!n || (n==1 && part[0]=='.') || (n==2 && part[0]=='.'&&part[1]=='.') || (c && basename))return 0;if(!c)return 1;part=p+1;}
        else if(!((c>='a'&&c<='z') || (c>='A'&&c<='Z') || (c>='0'&&c<='9') || c=='_' || c=='-' || c=='.'))return 0;}
}
static inline int pd_number(const char *p,uint64_t *out){if(!*p)return 0;uint64_t n=0;for(;*p;++p){if(*p<'0'||*p>'9'||n>(UINT64_MAX-(*p-'0'))/10)return 0;n=n*10+*p-'0';}*out=n;return 1;}
static inline int pd_name(const char *p){if(p[0]=='/' && p[1]=='@')p+=2;else if(p[0]=='@')++p;else return 0;return pd_path(p,1);}
static inline int pd_hex(char c){return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1;}
/* Parse an owned NUL-terminated text buffer; pointers remain within that buffer.
 * Unknown/duplicate fields and unsupported versions fail closed. */
static inline int provider_definition_parse(char *text,size_t bytes,struct provider_definition *d){
    *d=(struct provider_definition){.stack_bytes=65536};unsigned seen=0;
    for(size_t i=0;i<bytes;++i)if(!text[i] || ((unsigned char)text[i]<32 && text[i]!='\n' && text[i]!='\r' && text[i]!='\t') || (unsigned char)text[i]>=127)return 0;
    size_t at=0;while(at<bytes){size_t begin=at;while(at<bytes && text[at]!='\n')++at;size_t end=at;if(at<bytes)text[at++]=0;if(end>begin && text[end-1]=='\r')text[--end]=0;while(begin<end && (text[begin]==' '||text[begin]=='\t'))++begin;while(end>begin && (text[end-1]==' '||text[end-1]=='\t'))text[--end]=0;if(begin==end || text[begin]=='#')continue;
        size_t equal=begin;while(equal<end && text[equal]!='=')++equal;if(equal==end)return 0;text[equal]=0;char *key=text+begin,*value=text+equal+1;unsigned bit=0;uint64_t n;
        if(pd_same(key,"version")){bit=1;if(!pd_same(value,"1"))return 0;}
        else if(pd_same(key,"executable")){bit=2;if(!pd_path(value,0))return 0;d->executable=value;}
        else if(pd_same(key,"manifest")){bit=4;if(!pd_path(value,0))return 0;d->manifest=value;}
        else if(pd_same(key,"ready")){bit=8;if(*value && !pd_name(value))return 0;d->ready=value;}
        else if(pd_same(key,"installation_id")){bit=16;size_t length=0;while(value[length])++length;if(length!=32)return 0;unsigned any=0;for(unsigned i=0;i<16;++i){int a=pd_hex(value[i*2]),b=pd_hex(value[i*2+1]);if(a<0 || b<0)return 0;d->identity[i]=(a<<4)|b;any|=d->identity[i];}if(!any)return 0;}
        else if(pd_same(key,"cpu")){bit=32;if(!pd_number(value,&n) || n>UINT32_MAX)return 0;d->cpu=n;}
        else if(pd_same(key,"stack_bytes")){bit=64;if(!pd_number(value,&n) || !n)return 0;d->stack_bytes=n;}
        else if(pd_same(key,"files")){bit=128;d->files=value;}
        else return 0;
        if(seen&bit)return 0;
        seen|=bit;
    }return (seen&23)==23;
}
#endif
