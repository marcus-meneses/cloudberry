#ifndef CLOUDBERRY_INTERFACE_ADMISSION_H
#define CLOUDBERRY_INTERFACE_ADMISSION_H
#include "handle.h"
struct cb_admission {uint64_t process,grant_ns;uint8_t installation[16];};
/* Kernel-generated canonical JSON context; never caller payload. */
static inline int cb_admission_literal(const unsigned char *p,size_t bytes,size_t *at,const char *s){for(;*s;++s){if(*at>=bytes || p[(*at)++]!=(unsigned char)*s)return 0;}return 1;}
static inline int cb_admission_number(const unsigned char *p,size_t bytes,size_t *at,uint64_t *n){size_t begin=*at;*n=0;while(*at<bytes && p[*at]>='0'&&p[*at]<='9'){unsigned d=p[(*at)++]-'0';if(*n>(UINT64_MAX-d)/10)return 0;*n=*n*10+d;}return *at>begin;}
static inline int cb_admission_parse(const void *data,size_t bytes,struct cb_admission *out){
    if(!data || !out)return 0;
    const unsigned char *p=(const unsigned char *)data;size_t at=0;struct cb_admission a;unsigned char *zero=(unsigned char *)&a;for(size_t i=0;i<sizeof(a);++i)zero[i]=0;
    if(!cb_admission_literal(p,bytes,&at,"{\"input_stream\":\"boot:1\",\"grant_ns\":") || !cb_admission_number(p,bytes,&at,&a.grant_ns) || !cb_admission_literal(p,bytes,&at,",\"process\":") || !cb_admission_number(p,bytes,&at,&a.process) || !a.process || !cb_admission_literal(p,bytes,&at,",\"installation\":\""))return 0;
    for(unsigned i=0;i<32;++i){if(at>=bytes)return 0;unsigned c=p[at++],v=c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:16;if(v==16)return 0;a.installation[i/2]|=(uint8_t)(v<<(i%2?0:4));}
    if(!cb_admission_literal(p,bytes,&at,"\"}") || at!=bytes)return 0;
    *out=a;return 1;
}
#endif
