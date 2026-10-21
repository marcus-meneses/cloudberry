#ifndef CLOUDBERRY_INSTALLER_BUNDLE_H
#define CLOUDBERRY_INSTALLER_BUNDLE_H
#include "../../interfaces/installer.h"
#include "vendor/monocypher/monocypher-ed25519.h"
struct bundle {unsigned char *data;size_t bytes;uint64_t name,manifest,code,resources,flags,stack;size_t resources_at;unsigned char fingerprint[64];};
static int bundle_path(const unsigned char *p,size_t bytes){if(!bytes || bytes>4095 || p[0]=='/' || p[bytes-1]=='/')return 0;size_t begin=0;for(size_t i=0;i<=bytes;++i){if(i==bytes || p[i]=='/'){size_t n=i-begin;if(!n || n>255 || (n==1 && p[begin]=='.') || (n==2 && p[begin]=='.' && p[begin+1]=='.'))return 0;begin=i+1;}else if(!p[i] || p[i]<32 || p[i]=='\\')return 0;}return 1;}
static cb_status_t bundle_decode(struct bundle *b,unsigned char *data,size_t bytes,const unsigned char key[32]){
    if(bytes<176 || !cb_install_equal(data,"CBPKG001",8) || cb_reg_u64(data+8)!=bytes)return CB_BAD_REQUEST;
    if(!cb_install_equal(data+80,key,32) || crypto_ed25519_check(data+bytes-64,key,data,bytes-64))return CB_DENIED;
    *b=(struct bundle){.data=data,.bytes=bytes,.name=cb_reg_u64(data+16),.manifest=cb_reg_u64(data+24),.code=cb_reg_u64(data+32),.resources=cb_reg_u64(data+40),.flags=cb_reg_u64(data+48),.stack=cb_reg_u64(data+56)};
    if(!b->name || b->name>207 || !b->manifest || !b->code || (b->flags!=1 && b->flags!=3) || b->stack<4096 || !cb_frame_fits(176,b->name,b->manifest,bytes) || b->code>bytes-176-b->name-b->manifest)return CB_BAD_REQUEST;
    unsigned any=0;for(unsigned i=0;i<16;++i)any|=data[64+i];if(!any)return CB_BAD_REQUEST;
    for(size_t i=0;i<b->name;++i){unsigned c=data[112+i];if(!((c>='a'&&c<='z') || (c>='A'&&c<='Z') || (c>='0'&&c<='9') || (i && (c=='_' || c=='-'))))return CB_BAD_REQUEST;}
    b->resources_at=112+b->name+b->manifest+b->code;size_t at=b->resources_at,end=bytes-64;if(b->resources>(end-at)/16)return CB_BAD_REQUEST;
    for(uint64_t i=0;i<b->resources;++i){if(end-at<16)return CB_BAD_REQUEST;uint64_t n=cb_reg_u64(data+at),size=cb_reg_u64(data+at+8);if(n>end-at-16 || size>end-at-16-n || !bundle_path(data+at+16,n))return CB_BAD_REQUEST;
        for(size_t previous=b->resources_at;previous<at;){size_t pn=cb_reg_u64(data+previous),pb=cb_reg_u64(data+previous+8),shared=pn<n?pn:n;const unsigned char *p=data+previous+16,*q=data+at+16;if(cb_install_equal(p,q,shared) && (pn==n || (pn<n?q[pn]=='/':p[n]=='/')))return CB_BAD_REQUEST;previous+=16+pn+pb;}at+=16+n+size;}
    if(at!=end)return CB_BAD_REQUEST;
    crypto_sha512(b->fingerprint,data,bytes);return CB_OK;
}
#endif
