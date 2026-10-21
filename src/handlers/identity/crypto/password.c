#include "password.h"
#include <tinycrypt/sha256.h>
void id_wipe(void *p,size_t n){volatile unsigned char *v=p;while(n--)*v++=0;}
int id_password_valid(const unsigned char *p,size_t n){
    if(!n || n>256)return 0;
    unsigned count=0;for(size_t i=0;i<n;){unsigned c=p[i++],more;uint32_t v,min;if(c<0x80){if(!c)return 0;more=0;v=c;min=0;}else if(c>=0xc2 && c<=0xdf){more=1;v=c&31;min=0x80;}else if(c>=0xe0 && c<=0xef){more=2;v=c&15;min=0x800;}else if(c>=0xf0 && c<=0xf4){more=3;v=c&7;min=0x10000;}else return 0;if(more>n-i)return 0;while(more--){c=p[i++];if((c&0xc0)!=0x80)return 0;v=(v<<6)|(c&63);}if(v<min || v>0x10ffff || (v>=0xd800 && v<=0xdfff) || ++count>64)return 0;}return 1;
}
static int digest(const struct tc_sha256_state_struct *inner,const struct tc_sha256_state_struct *outer,const void *data,size_t n,unsigned char out[32]){
    struct tc_sha256_state_struct state=*inner;unsigned char temp[32];int ok=tc_sha256_update(&state,data,n) && tc_sha256_final(temp,&state);state=*outer;ok=ok && tc_sha256_update(&state,temp,32) && tc_sha256_final(out,&state);id_wipe(temp,sizeof(temp));id_wipe(&state,sizeof(state));return ok;
}
int id_password_derive(const void *password,size_t bytes,const unsigned char salt[32],uint32_t rounds,unsigned char out[32]){
    if(!password || !bytes || bytes>256 || !rounds)return 0;
    unsigned char key[64]={0},pad[64],u[32],first[36];struct tc_sha256_state_struct inner,outer;
    int ok=1;if(bytes>64){ok=tc_sha256_init(&inner) && tc_sha256_update(&inner,password,bytes) && tc_sha256_final(key,&inner);}else for(size_t i=0;i<bytes;++i)key[i]=((const unsigned char *)password)[i];
    for(unsigned i=0;i<64;++i)pad[i]=key[i]^0x36;
    ok=ok && tc_sha256_init(&inner) && tc_sha256_update(&inner,pad,64);
    for(unsigned i=0;i<64;++i)pad[i]=key[i]^0x5c;
    ok=ok && tc_sha256_init(&outer) && tc_sha256_update(&outer,pad,64);
    for(unsigned i=0;i<32;++i)first[i]=salt[i];
    first[32]=first[33]=first[34]=0;first[35]=1;
    ok=ok && digest(&inner,&outer,first,sizeof(first),u);for(unsigned i=0;i<32;++i)out[i]=u[i];
    for(uint32_t r=1;r<rounds && ok;++r){ok=digest(&inner,&outer,u,32,u);for(unsigned i=0;i<32;++i)out[i]^=u[i];}
    id_wipe(key,sizeof(key));id_wipe(pad,sizeof(pad));id_wipe(u,sizeof(u));id_wipe(first,sizeof(first));id_wipe(&inner,sizeof(inner));id_wipe(&outer,sizeof(outer));if(!ok)id_wipe(out,32);return ok;
}
int id_password_equal(const unsigned char a[32],const unsigned char b[32]){unsigned value=0;for(unsigned i=0;i<32;++i)value|=a[i]^b[i];return value==0;}
