#ifndef CLOUDBERRY_IDENTITY_FORMAT_H
#define CLOUDBERRY_IDENTITY_FORMAT_H
#include <stdint.h>
#include <stddef.h>
#define ID_HEADER 64u
#define ID_KDF_PBKDF2_SHA256 1u
#define ID_KDF_MIN_ROUNDS 600000u
struct id_blob {unsigned char *data;size_t bytes;};
static inline uint32_t id_u32(const void *v){const unsigned char *p=v;return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static inline uint64_t id_u64(const void *v){const unsigned char *p=v;return id_u32(p)|((uint64_t)id_u32(p+4)<<32);}
static inline void id_put32(void *v,uint32_t n){unsigned char *p=v;for(unsigned i=0;i<4;++i)p[i]=(unsigned char)(n>>(i*8));}
static inline void id_put64(void *v,uint64_t n){unsigned char *p=v;id_put32(p,(uint32_t)n);id_put32(p+4,(uint32_t)(n>>32));}
static inline int id_equal(const void *a,const void *b,size_t n){const unsigned char *x=a,*y=b;for(size_t i=0;i<n;++i)if(x[i]!=y[i])return 0;return 1;}
static inline uint32_t id_crc(const unsigned char *p,size_t n,size_t zero_at){uint32_t c=UINT32_MAX;for(size_t i=0;i<n;++i){c^=i>=zero_at && i-zero_at<4?0:p[i];for(unsigned j=0;j<8;++j)c=(c>>1)^(0xedb88320u&-(c&1));}return ~c;}
/* Common 64-byte header: magic[8], major:u16, minor:u16, header:u32,
 * total:u64, generation:u64, count:u64, next ID:u64, CRC32:u32, reserved[12].
 * CRC covers the complete file with its own four bytes zeroed. */
static inline const char *id_magic(unsigned kind){return kind==0?"CBUSER01":kind==1?"CBGRUP01":"CBCRED01";}
static inline int id_name(const unsigned char *p,size_t n){if(!n)return 0;for(size_t i=0;i<n;++i)if(!((p[i]>='a'&&p[i]<='z') || p[i]=='_' || (i && ((p[i]>='0'&&p[i]<='9') || p[i]=='-'))))return 0;return 1;}
static inline const unsigned char *id_find(const struct id_blob *b,uint64_t id){size_t at=ID_HEADER;while(at<b->bytes){const unsigned char *r=b->data+at;uint64_t n=id_u64(r+8);if(n==id)return r;if(n>id)return NULL;at+=id_u32(r);}return NULL;}
int id_file_valid(const struct id_blob *,unsigned);
int id_set_valid(const struct id_blob [3]);
int id_transition_valid(const struct id_blob [3],const struct id_blob [3]);
#endif
