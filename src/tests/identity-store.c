#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../handlers/identity/store.h"
static struct id_blob disk[4];static int writes,fail_at,readonly;static long allocations;
static void *alloc(void *c,size_t n){(void)c;void *p=malloc(n);if(p)++allocations;return p;}
static void drop(void *c,void *p){(void)c;if(p){--allocations;free(p);}}
static unsigned index_of(const char *p){for(unsigned i=0;i<4;++i)if(!strcmp(p,id_path(i)))return i;assert(0);return 0;}
static cb_status_t read_disk(void *c,const char *p,struct id_blob *out){unsigned i=index_of(p);out->bytes=disk[i].bytes;out->data=out->bytes?alloc(c,out->bytes):NULL;if(out->bytes)memcpy(out->data,disk[i].data,out->bytes);return CB_OK;}
static cb_status_t write_disk(void *c,const char *p,const unsigned char *data,size_t n){(void)c;if(readonly)return CB_READ_ONLY;unsigned i=index_of(p);++writes;size_t saved=writes==fail_at?n/2:n;free(disk[i].data);disk[i]=(struct id_blob){saved?malloc(saved):NULL,saved};if(saved)memcpy(disk[i].data,data,saved);return writes==fail_at?CB_RESOURCE_FAILURE:CB_OK;}
static struct id_store store(void){return (struct id_store){.allocate=alloc,.release=drop,.read=read_disk,.write=write_disk};}
static void destroy(struct id_store *s){for(unsigned k=0;k<3;++k)drop(NULL,s->files[k].data);}
static struct id_blob fixture(const char *dir,unsigned k){char path[4096];snprintf(path,sizeof(path),"%s/%s",dir,k==0?"users.bin":k==1?"groups.bin":"credentials.bin");FILE *f=fopen(path,"rb");assert(f);fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);struct id_blob b={malloc(n),(size_t)n};assert(fread(b.data,1,n,f)==(size_t)n);fclose(f);return b;}
static void checksum(struct id_blob *b){id_put32(b->data+48,id_crc(b->data,b->bytes,48));}
int main(int argc,char **argv){assert(argc==2);struct id_blob original[3],next[3];for(unsigned k=0;k<3;++k){original[k]=fixture(argv[1],k);next[k]=(struct id_blob){malloc(original[k].bytes),original[k].bytes};memcpy(next[k].data,original[k].data,next[k].bytes);id_put64(next[k].data+24,2);checksum(&next[k]);}assert(id_set_valid(original) && id_transition_valid(original,next));
    next[0].data[8]=2;assert(!id_set_valid(next));next[0].data[8]=1;checksum(&next[0]);
    id_put64(next[0].data+40,0);checksum(&next[0]);assert(!id_transition_valid(original,next));id_put64(next[0].data+40,1);checksum(&next[0]);
    next[0].data[ID_HEADER+32]='x';checksum(&next[0]);assert(!id_set_valid(next));next[0].data[ID_HEADER+32]='r';checksum(&next[0]);
    next[2].data[ID_HEADER+16]=1;checksum(&next[2]);assert(!id_set_valid(next));memcpy(next[2].data,original[2].data,next[2].bytes);id_put64(next[2].data+24,2);checksum(&next[2]);
    for(int cut=0;cut<=5;++cut){for(unsigned k=0;k<4;++k){free(disk[k].data);disk[k]=(struct id_blob){0};if(k<3){disk[k].bytes=original[k].bytes;disk[k].data=malloc(disk[k].bytes);memcpy(disk[k].data,original[k].data,disk[k].bytes);}}
        writes=fail_at=0;struct id_store first=store();assert(id_store_load(&first)==CB_OK);writes=0;fail_at=cut;cb_status_t status=id_store_commit(&first,next);assert(status==(cut?CB_RESOURCE_FAILURE:CB_OK));assert(first.failed==(unsigned)(cut!=0));destroy(&first);
        fail_at=0;struct id_store recovered=store();assert(id_store_load(&recovered)==CB_OK);assert(id_u64(recovered.files[0].data+24)==(cut==1?1:2));assert(id_set_valid(recovered.files));destroy(&recovered);assert(!allocations);
    }
    struct id_store ro=store();assert(id_store_load(&ro)==CB_OK);for(unsigned k=0;k<3;++k){id_put64(next[k].data+24,3);checksum(&next[k]);}readonly=1;assert(id_store_commit(&ro,next)==CB_READ_ONLY && !ro.failed && id_u64(ro.files[0].data+24)==2);readonly=0;destroy(&ro);
    for(unsigned k=0;k<3;++k){free(original[k].data);free(next[k].data);}for(unsigned k=0;k<4;++k)free(disk[k].data);assert(!allocations);puts("PASS identity: binary validation, root invariants, generation/high-water checks, commit and replay across every interrupted write");
}
