#include "../../interfaces/identity-format.h"
int id_file_valid(const struct id_blob *b,unsigned kind){
    if(!b || !b->data || b->bytes<ID_HEADER || kind>2)return 0;
    const unsigned char *p=b->data;if(!id_equal(p,id_magic(kind),8) || p[8]!=1 || p[9] || p[10] || p[11] || id_u32(p+12)!=ID_HEADER || id_u64(p+16)!=b->bytes || !id_u64(p+24) || id_u32(p+48)!=id_crc(p,b->bytes,48))return 0;
    for(unsigned i=52;i<ID_HEADER;++i)if(p[i])return 0;
    uint64_t count=id_u64(p+32),next=id_u64(p+40),previous=0;size_t at=ID_HEADER;
    for(uint64_t i=0;i<count;++i){if(b->bytes-at<32)return 0;const unsigned char *r=p+at;size_t bytes=id_u32(r);uint64_t id=id_u64(r+8);if(bytes<32 || bytes>b->bytes-at || (i && id<=previous) || (kind<2 && id>=next))return 0;previous=id;
        if(kind==0){if(id_u32(r+4)>1 || id_u32(r+28) || id_u32(r+24)!=bytes-32 || !id_name(r+32,bytes-32))return 0;}
        else if(kind==1){size_t name=id_u32(r+16),members=id_u32(r+20);if(id_u32(r+4)>1 || id_u64(r+24) || name>bytes-32 || members>(bytes-32-name)/8 || 32+name+members*8!=bytes || !id_name(r+32,name))return 0;for(size_t j=1;j<members;++j)if(id_u64(r+32+name+(j-1)*8)>=id_u64(r+32+name+j*8))return 0;}
        else if(bytes!=96 || id_u32(r+4)!=ID_KDF_PBKDF2_SHA256 || id_u32(r+16)<ID_KDF_MIN_ROUNDS || id_u32(r+20)!=32 || id_u32(r+24)!=32 || id_u32(r+28))return 0;
        if(kind<2){size_t n=id_u32(r+(kind?16:24)),prior=ID_HEADER;for(uint64_t j=0;j<i;++j){const unsigned char *old=p+prior;if(id_u32(old+(kind?16:24))==n && id_equal(old+32,r+32,n))return 0;prior+=id_u32(old);}}
        at+=bytes;
    }return at==b->bytes && (kind==2?next==0:next>0);
}
int id_set_valid(const struct id_blob b[3]){
    for(unsigned k=0;k<3;++k)if(!id_file_valid(&b[k],k) || id_u64(b[k].data+24)!=id_u64(b[0].data+24))return 0;
    const unsigned char *root=id_find(&b[0],0);if(!root || id_u32(root+4)!=1 || id_u64(root+16) || id_u32(root+24)!=4 || !id_equal(root+32,"root",4) || !id_find(&b[2],0))return 0;
    const char *names[]={"root","admin","users"};const size_t sizes[]={4,5,5};for(unsigned k=0;k<3;++k){const unsigned char *g=id_find(&b[1],k);if(!g || id_u32(g+4)!=1 || id_u32(g+16)!=sizes[k] || !id_equal(g+32,names[k],sizes[k]))return 0;if(!k && (id_u32(g+20)!=1 || id_u64(g+32+sizes[k])))return 0;}
    for(size_t at=ID_HEADER;at<b[0].bytes;at+=id_u32(b[0].data+at)){const unsigned char *u=b[0].data+at;if(!id_find(&b[1],id_u64(u+16)) || (id_u64(u+8) && !id_u64(u+16)))return 0;}
    for(size_t at=ID_HEADER;at<b[1].bytes;at+=id_u32(b[1].data+at)){const unsigned char *g=b[1].data+at;size_t n=id_u32(g+16);for(uint32_t i=0;i<id_u32(g+20);++i)if(!id_find(&b[0],id_u64(g+32+n+(size_t)i*8)))return 0;}
    for(size_t at=ID_HEADER;at<b[2].bytes;at+=id_u32(b[2].data+at))if(!id_find(&b[0],id_u64(b[2].data+at+8)))return 0;
    return 1;
}
int id_transition_valid(const struct id_blob old[3],const struct id_blob next[3]){
    if(!id_set_valid(next) || id_u64(old[0].data+24)==UINT64_MAX || id_u64(next[0].data+24)!=id_u64(old[0].data+24)+1)return 0;
    for(unsigned k=0;k<2;++k){uint64_t floor=id_u64(old[k].data+40);if(id_u64(next[k].data+40)<floor)return 0;for(size_t at=ID_HEADER;at<next[k].bytes;at+=id_u32(next[k].data+at)){uint64_t id=id_u64(next[k].data+at+8);if(id<floor && !id_find(&old[k],id))return 0;}}
    return 1;
}
